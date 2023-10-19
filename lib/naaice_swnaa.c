/**************************************************************************//**
 *
 *    `7MN.   `7MF'     db            db      `7MMF'  .g8"""bgd `7MM"""YMM  
 *      MMN.    M      ;MM:          ;MM:       MM  .dP'     `M   MM    `7  
 *      M YMb   M     ,V^MM.        ,V^MM.      MM  dM'       `   MM   d    
 *      M  `MN. M    ,M  `MM       ,M  `MM      MM  MM            MMmmMM    
 *      M   `MM.M    AbmmmqMA      AbmmmqMA     MM  MM.           MM   Y  , 
 *      M     YMM   A'     VML    A'     VML    MM  `Mb.     ,'   MM     ,M 
 *    .JML.    YM .AMA.   .AMMA..AMA.   .AMMA..JMML.  `"bmmmd'  .JMMmmmmMMM 
 * 
 *  Network-Attached Accelerators for Energy-Efficient Heterogeneous Computing
 * 
 * naaice_swnaa.c
 *
 * Implementations for functions in naaice_swnaa.h.
 * 
 * Florian Mikolajczak, florian.mikolajczak@uni-potsdam.de
 * Dylan Everingham, everingham@zib.de
 * 
 * 12-10-2023
 * 
 *****************************************************************************/

// Enable debug messages.
#define DEBUG 1

/* Dependencies **************************************************************/

#include <debug.h>
#include <errno.h>
#include <naaice_swnaa.h>
#include <naaice.h>

/* Function Implementations **************************************************/

int naaice_swnaa_init_communication_context(
  struct naaice_communication_context **comm_ctx, uint16_t port) {

  debug_print("In naaice_swnaa_init_communication_context\n");

  // Allocate memory for the communication context.
  debug_print("Allocating communication context.\n");
  *comm_ctx = (struct naaice_communication_context*)
    malloc(sizeof(struct naaice_communication_context));
  if (comm_ctx == NULL) {
    fprintf(stderr,
            "Failed to allocate memory for communication context. Exiting.");
    return -1;
  }

  // Make an event channel, checking for allocation success.
  debug_print("Making event channel.\n");
  (*comm_ctx)->ev_channel = rdma_create_event_channel();
  if (!(*comm_ctx)->ev_channel) {
    fprintf(stderr, "Failed to create an RDMA event channel.\n");
    return -1;
  }

  // Make a communication ID, checking for allocation success.
  debug_print("Making communication ID.\n");
  struct rdma_cm_id *rdma_comm_id;
  if (rdma_create_id((*comm_ctx)->ev_channel, &rdma_comm_id, NULL,
        RDMA_PS_TCP) == -1) {
    fprintf(stderr, "Failed to create an RDMA communication id.\n");
    return -1;
  }
  (*comm_ctx)->ibv_ctx = rdma_comm_id->verbs;

  // TODO: This is circular. Why do it like this?
  rdma_comm_id->context = (*comm_ctx);

  // Initialize fields of the communication context.
  (*comm_ctx)->state = READY;
  (*comm_ctx)->events_to_ack = 0;
  (*comm_ctx)->events_acked = 0;
  (*comm_ctx)->id = rdma_comm_id;
  (*comm_ctx)->no_local_mrs = 0;
  (*comm_ctx)->mr_return_idx = 0;
  (*comm_ctx)->rdma_writes_done = 0;
  (*comm_ctx)->comm_setup_complete = false;
  (*comm_ctx)->address_resolution_complete = false;
  (*comm_ctx)->route_resolution_complete = false;
  (*comm_ctx)->connection_requests_complete = false;
  (*comm_ctx)->connection_established_complete = false;
  (*comm_ctx)->routine_complete = false;

  // The memory region used for MRSP is allocated here, but the ones for the
  // parameters and metadata are not until after MRSP is complete (when their
  // sizes are known).
  (*comm_ctx)->mr_local_data = NULL;
  
  debug_print("Allocating memory region for MRSP.\n");
  (*comm_ctx)->mr_local_message = calloc(1, sizeof(struct naaice_mr_local));
  if ((*comm_ctx)->mr_local_message == NULL) {
    fprintf(stderr,
            "Failed to allocate local memory for MRSP messages.\n");
    return -1;
  }
  (*comm_ctx)->mr_local_message->addr = calloc(1, MR_SIZE_MR_EXHANGE);
  if ((*comm_ctx)->mr_local_message->addr == NULL) {
    fprintf(stderr,
            "Failed to allocate local memory for MRSP messages.\n");
    return -1;
  }

  // Configure connection.
  // TODO: make port flexible?
  debug_print("Configuring connection.\n");
  struct sockaddr loc_addr;
  memset(&loc_addr, 0, sizeof(loc_addr));
  loc_addr.sa_family = AF_INET;
  ((struct sockaddr_in *)&loc_addr)->sin_port = htons(port);

  // Bind communication ID to local address.
  if (rdma_bind_addr(rdma_comm_id, &loc_addr)) {
    fprintf(stderr, "Binding communication ID to local address failed.\n");
    fprintf(stderr, "errno: %d\n", errno);
    return -1;
  }

  // Listen on the port.
  if (rdma_listen(rdma_comm_id, 10)) { // Backlog queue length 10.
    fprintf(stderr, "Listening on specified port failed.\n");
    return -1;
  }

  int port_num = ntohs(rdma_get_src_port(rdma_comm_id));
  debug_print("Listening on port %d.\n", port_num);

  return 0;
}

int naaice_swnaa_setup_connection(
  struct naaice_communication_context *comm_ctx) {

  debug_print("In naaice_swnaa_setup_connection\n");

  // Can simply call same logic used on the host side here.
  return naaice_setup_connection(comm_ctx);
}

int naaice_swnaa_init_mrsp(struct naaice_communication_context *comm_ctx) {

  debug_print("In naaice_swnaa_init_mrsp\n");

  comm_ctx->rdma_writes_done = 0;
  comm_ctx->events_to_ack = 0;
  comm_ctx->events_acked = 0;

  // Wait for memory region announcement and request from the host.
  naaice_swnaa_post_recv_mrsp(comm_ctx);

  return 0;
}

int naaice_swnaa_post_recv_mrsp(
  struct naaice_communication_context *comm_ctx) {

  debug_print("In naaice_swnaa_post_recv_mrsp\n");

  // Can simply call same logic used on the host side here.
  return naaice_post_recv_mrsp(comm_ctx);
}

int naaice_swnaa_handle_work_completion_mrsp(struct ibv_wc *wc,
  struct naaice_communication_context *comm_ctx) {

  debug_print("In naaice_swnaa_handle_work_completion_mrsp\n");

  // If the work completion status is not success, return with error.
  if (wc->status != IBV_WC_SUCCESS) {
    fprintf(stderr,
            "Status is not IBV_WC_SUCCESS. Status %d for operation %d.\n",
            wc->status, wc->opcode);
    return -1;
  }

  // If we're recieving an MRSP packet...
  if (wc->opcode == IBV_WC_RECV) {

    // If we're in the right state to recieve an MRSP packet...
    if (comm_ctx->state == CONNECTED) {

      // The message should have been written to the memory region we allocated
      // for MRRSP messages. grab the header from there.
      struct naaice_mr_hdr *msg =
          (struct naaice_mr_hdr *)comm_ctx->mr_local_message->addr;
      
      // If the message was an announce...
      // TODO: probably no longer necessary, host always sends A+R.
      if (msg->type == MSG_MR_A) {

        // Handle the packet.
        if(naaice_swnaa_handle_mr_announce(comm_ctx)) { return -1; }

        // Post a recieve for memory region data.
        return naaice_swnaa_post_recv_data(comm_ctx);
      }

      // If the message was an announce + request...
      else if (msg->type == MSG_MR_AAR) {

        if (naaice_swnaa_handle_mr_announce_and_request(comm_ctx)) {

          // If an error occurs, send an error message to the host.
          naaice_swnaa_send_message(comm_ctx, MSG_MR_ERR, 1);
          return -1;
        }

        // Otherwise send an announcement back.
        naaice_swnaa_send_message(comm_ctx, MSG_MR_A, 0);

        // Post a recieve for memory region data.
        naaice_swnaa_post_recv_data(comm_ctx);
        return 0;
      }

      // Return with error if a remote error has occured.
      else if (msg->type == MSG_MR_ERR) {
        struct naaice_mr_error *err =
            (struct naaice_mr_error *)(comm_ctx->mr_local_message->addr +
                                       sizeof(struct naaice_mr_error));
        fprintf(stderr,
                "Remote node encountered error in message exchange: %d\n",
                err->code);
        return -1;
      }
    }

    // Otherwise nothing needs to be done.
    return 0;
  }

  // NAA-side of MRSP done. Go to next state.
  else if (wc->opcode == IBV_WC_SEND) {
    comm_ctx->state = WAIT_DATA;
    return 0;
  }

  // For all other completion codes...
  else {
    fprintf(stderr,
      "work completion opcode (wc opcode): %d, not handled. Disconnecting.\n",
      wc->opcode);
    return -1;
  }

  // If we've reached here, something has gone wrong.
  fprintf(stderr, "Unknown error while handling work completion for MRSP.\n");
  return -1;
}

int naaice_swnaa_poll_cq_nonblocking_mrsp(
  struct naaice_communication_context *comm_ctx) {

  debug_print("In naaice_swnaa_poll_cq_nonblocking_mrsp\n");

  struct ibv_cq *ev_cq;
  void *ev_ctx;

  // If there is still work to be done...
  if (comm_ctx->events_acked < comm_ctx->events_to_ack) {

    // Ensure completion channel is in non-blocking mode.
    int fd_flags = fcntl(comm_ctx->comp_channel->fd, F_GETFL);
    if (fcntl(comm_ctx->comp_channel->fd, F_SETFL,
          fd_flags | O_NONBLOCK) < 0) {
      fprintf(stderr, "Failed to change file descriptor of completion "
        "event channel.\n");
      return -1;
    }

    struct pollfd my_pollfd;
    int ms_timeout = 100;

    // Poll the completion channel, returning with flag unchanged if nothing
    // is recieved.
    my_pollfd.fd = comm_ctx->comp_channel->fd;
    my_pollfd.events = POLLIN;
    my_pollfd.revents = 0;

    if (poll(&my_pollfd, 1, ms_timeout) < 0) {
      fprintf(stderr, "No completion recieved.\n");
      return 0;
    }

    // If something is recieved, get the completion event.
    if (ibv_get_cq_event(comm_ctx->comp_channel, &ev_cq, &ev_ctx)) {
      fprintf(stderr, "Failed to get completion queue event.\n");
      return -1;
    }
    
    // Ack the completion event.
    ibv_ack_cq_events(ev_cq, comm_ctx->events_to_ack);
    struct ibv_wc wc[comm_ctx->events_to_ack];
    comm_ctx->events_acked = 0;

    // Poll the completion queue.
    int n_wcs = ibv_poll_cq(comm_ctx->cq,
                           comm_ctx->events_to_ack - comm_ctx->events_acked,
                           &wc[comm_ctx->events_acked]);
    // If ibv_poll_cq returns an error, return.
    if (n_wcs < 0) {
      fprintf(stderr, "ibv_poll_cq() failed.\n");
      return -1;
    }

    // Increment the count of acked events.
    comm_ctx->events_acked = comm_ctx->events_acked + n_wcs;
    printf("%d of %d events polled\n", comm_ctx->events_acked,
         comm_ctx->events_to_ack);
  
    // Request completion channel notifications.
    if (ibv_req_notify_cq(comm_ctx->cq, 0)) {
      fprintf(
          stderr,
          "Failed to request completion channel notifications on completion "
          "queue.\n");
      return -1;
    }

    // Handle the work completions.
    // TODO: Handle errors from this. If something goes wrong, disconnect?
    for (int i = 0; i < comm_ctx->events_acked; i++) {
      if (naaice_swnaa_handle_work_completion_mrsp(&wc[i], comm_ctx)) {
        fprintf(stderr, "Error while handling work completion.\n");
        return -1;
      }
    }

    // Check the stopping condition again.
    if (comm_ctx->events_acked < comm_ctx->events_to_ack) {
      comm_ctx->routine_complete = false;
    } else {
      comm_ctx->routine_complete = true;
    }
  }

  // Otherwise we are already done.
  else { comm_ctx->routine_complete = true; }

  return 0;
}

int naaice_swnaa_poll_cq_blocking_mrsp(
  struct naaice_communication_context *comm_ctx) {

  debug_print("In naaice_swnaa_poll_cq_blocking_mrsp\n");

  // TODO: consider a timeout here.
  while(!comm_ctx->routine_complete) {
    if (naaice_swnaa_poll_cq_nonblocking_mrsp(comm_ctx)) { return -1; }
  }

  return 0;
}

int naaice_swnaa_handle_mr_announce(
  struct naaice_communication_context *comm_ctx) {

  debug_print("In naaice_swnaa_handle_mr_announce\n");
  
  // First read the header.
  struct naaice_mr_dynamic_hdr *dyn =
      (struct naaice_mr_dynamic_hdr *)(comm_ctx->mr_local_message->addr +
                                       sizeof(struct naaice_mr_hdr));
  
  // Get the number of host memory regions.
  comm_ctx->no_peer_mrs = dyn->count;


  // Allocate memory to hold information about host memory regions.
  comm_ctx->mr_peer_data =
      calloc(comm_ctx->no_peer_mrs, sizeof(struct naaice_mr_peer));
  if (comm_ctx->mr_peer_data == NULL) {
    fprintf(stderr,
            "Failed to allocate memory remote memory region structure.\n");
    return -1;
  }

  // Pointer to current position in packet being read.
  struct naaice_mr_advertisement *mr;

  // For each host memory region...
  for (int i = 0; i < comm_ctx->no_peer_mrs; i++) {

    // Point to next position in the packet.
    mr = (struct naaice_mr_advertisement
              *)(comm_ctx->mr_local_message->addr +
                 (sizeof(struct naaice_mr_hdr) +
                  sizeof(struct naaice_mr_dynamic_hdr) +
                  (i + 1) * sizeof(struct naaice_mr_advertisement)));

    // Set fields.
    comm_ctx->mr_peer_data[i].addr = ntohll(mr->addr);
    comm_ctx->mr_peer_data[i].rkey = ntohl(mr->rkey);
    comm_ctx->mr_peer_data[i].size = ntohl(mr->size);

    /*
    printf("Peer MR %d: Addr: %lX, Size: %d, rkey: %d\n", i + 1, 
          (uintptr_t)peer[i].addr,
          peer[i].size, peer[i].rkey);
    */
  }

  return 0;
}

int naaice_swnaa_handle_mr_announce_and_request(
    struct naaice_communication_context *comm_ctx) {

  debug_print("In naaice_swnaa_handle_mr_announce_and_request\n");

    // First read the header.
  struct naaice_mr_dynamic_hdr *dyn =
      (struct naaice_mr_dynamic_hdr *)(comm_ctx->mr_local_message->addr +
                                       sizeof(struct naaice_mr_hdr));
  
  // Get the number of host memory regions.
  comm_ctx->no_peer_mrs = dyn->count;


  // Allocate memory to hold information about host memory regions.
  comm_ctx->mr_peer_data =
      calloc(comm_ctx->no_peer_mrs, sizeof(struct naaice_mr_peer));
  if (comm_ctx->mr_peer_data == NULL) {
    fprintf(stderr,
            "Failed to allocate memory for remote memory region "
            "structures.\n");
    return -1;
  }

  // Pointer to current position in packet being read.
  struct naaice_mr_advertisement *mr;

  // For each host memory region...
  for (int i = 0; i < comm_ctx->no_peer_mrs; i++) {

    // Point to next position in the packet.
    mr = (struct naaice_mr_advertisement
              *)(comm_ctx->mr_local_message->addr +
                 (sizeof(struct naaice_mr_hdr) +
                  sizeof(struct naaice_mr_dynamic_hdr) +
                  (i + 1) * sizeof(struct naaice_mr_advertisement)));

    // Set fields.
    comm_ctx->mr_peer_data[i].addr = ntohll(mr->addr);
    comm_ctx->mr_peer_data[i].rkey = ntohl(mr->rkey);
    comm_ctx->mr_peer_data[i].size = ntohl(mr->size);
  }

  // Now we need to allocate local memory regions based on this information.

  // The request packet includes the fields mrflags and fpgaaddress, which
  // specify how an FPGA-based NAA should handle memory region allocation.
  // However, in the software NAA, we can simply ignore these fields and
  // allocate as normal.

  comm_ctx->mr_local_data = calloc(comm_ctx->no_local_mrs, 
                              sizeof(struct naaice_mr_local));
  if (comm_ctx->mr_local_data == NULL) {
    fprintf(stderr,
            "Failed to allocate memory for local memory region structures.\n");
    return -1;
  }
  
  // For each memory region...
  for (int i = 0; i < (comm_ctx->no_local_mrs); i++) {

    // Allocate memory for the memory region. This will be written into later
    // during data transmission.
    posix_memalign((void **)&(comm_ctx->mr_local_data[i].addr),
                   sysconf(_SC_PAGESIZE), comm_ctx->mr_peer_data[i].size);
    if (comm_ctx->mr_local_data[i].addr == NULL) {
      fprintf(stderr,
              "Failed to allocate memory for local memory region buffer.\n");
      return -1;
    }

    // Register the memory region.
    comm_ctx->mr_local_data[i].ibv =
        ibv_reg_mr(comm_ctx->pd, comm_ctx->mr_local_data[i].addr,
                   comm_ctx->mr_peer_data[i].size,
                   (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));
    if (comm_ctx->mr_local_data[i].ibv == NULL) {
      fprintf(stderr, "Failed to register memory for local memory region.\n");
      return -1;
    }

    // Set the size of the memory region.
    comm_ctx->mr_local_data[i].size = comm_ctx->mr_local_data[i].ibv->length;
  }

  return 0;
}

int naaice_swnaa_send_message(struct naaice_communication_context *comm_ctx,
  enum message_id message_type, uint8_t errorcode) {

  debug_print("In naaice_swnaa_send_message\n");
  
  // This is the same as naaice_send_message, except that request messages are
  // not allowed to be sent from the server to host.

  // All messages start with a header.
  // We use a dedicated memory region to construct the message, allocated in
  // naaice_init_communication_context.
  struct naaice_mr_hdr *msg =
      (struct naaice_mr_hdr*) comm_ctx->mr_local_message->addr;

  // Keep track of message size as we add fields.
  int msg_size = 0;
  msg_size += sizeof(struct naaice_mr_hdr);

  // Set message type.
  msg->type = message_type;

  // MRSP Messages: Advertisement+Request or Advertisement.

  // If we're sending an advertisement packet (for MRSP)...
  if (msg->type == MSG_MR_A) {

    // Add a dynamic header.
    struct naaice_mr_dynamic_hdr *dyn =
        (struct naaice_mr_dynamic_hdr *)(msg + sizeof(struct naaice_mr_hdr));
    msg_size += sizeof(struct naaice_mr_dynamic_hdr);
    dyn->count = comm_ctx->no_local_mrs;
    dyn->padding[0] = 0;
    dyn->padding[1] = 0;

    // Pointer to the current position in the message being constructed.
    struct naaice_mr_advertisement *curr;

    // For each memory region...
    for (int i = 0; i < comm_ctx->no_local_mrs; i++) {

      // Point to next position in the packet.
      curr = (struct naaice_mr_advertisement*)
                    (msg + sizeof(struct naaice_mr_hdr) +
                    sizeof(struct naaice_mr_dynamic_hdr) +
                    i * sizeof(struct naaice_mr_advertisement));

      // Set fields of the packet relating to this memory region.
      curr->addr = htonll((uintptr_t)comm_ctx->mr_local_data[i].addr);
      curr->size = htonl(comm_ctx->mr_local_data[i].ibv->length);
      curr->rkey = htonl(comm_ctx->mr_local_data[i].ibv->rkey);

      /*
      printf("Local MR %d: Addr: %lX, Size: %ld, rkey: %d\n", i + 1,
             (uintptr_t)comm_ctx->mr_local_data[i].addr,
             comm_ctx->mr_local_data[i].ibv->length,
             comm_ctx->mr_local_data[i].ibv->rkey);
      */

      // Update packet size.
      msg_size += sizeof(struct naaice_mr_advertisement);
    }
  }
    
  // If we're sending an error message...
  if (message_type == MSG_MR_ERR) {

    // Insert error packet. No dynamic header for this message type.
    struct naaice_mr_error *err =
        (struct naaice_mr_error*) (msg + sizeof(struct naaice_mr_hdr));

    // TODO: Specify meanings of different error codes. 
    // Currently only use one (non-zero).
    err->code = errorcode;

    // Update packet size.
    msg_size += sizeof(struct naaice_mr_error);
  }

  // Construct scatter/gather elements.
  struct ibv_sge sge;
  sge.addr = (uintptr_t)comm_ctx->mr_local_message->addr;
  sge.length = msg_size;
  sge.lkey = comm_ctx->mr_local_message->ibv->lkey;

  // Construct write request, which has the scatter/gather elements.
  struct ibv_send_wr wr, *bad_wr = NULL;
  memset(&wr, 0, sizeof(wr));
  wr.wr_id = msg->type; //(uintptr_t)comm_ctx;
  wr.opcode = IBV_WR_SEND;
  wr.sg_list = &sge;
  wr.num_sge = 1;
  //wr.send_flags = IBV_SEND_SIGNALED;
  wr.send_flags = IBV_SEND_SOLICITED;

  // Indicate that we expect one packet in response.
  comm_ctx->events_to_ack = 1;
  comm_ctx->events_acked = 0;

  // Send the packet.
  int post_result = ibv_post_send(comm_ctx->qp, &wr, &bad_wr);
  if (post_result) {
    fprintf(stderr, "Posting send for MRSP failed with error %d.\n",
      post_result);
    return -1;
  }

  return 0;
}

int naaice_swnaa_post_recv_data(
  struct naaice_communication_context *comm_ctx) {

  debug_print("In naaice_swnaa_post_recv_data\n");

  // Construct the recieve request and scatter/gather elements,
  // one for each memory region to be written to.

  struct ibv_recv_wr wr[comm_ctx->no_local_mrs], *bad_wr = NULL;
  struct ibv_sge sge[comm_ctx->no_local_mrs];

  for (int i = 0; i < comm_ctx->no_local_mrs; i++) {
    sge[i].addr = (uintptr_t)comm_ctx->mr_local_data[i].addr;
    sge[i].length = comm_ctx->mr_local_data[i].ibv->length;
    sge[i].lkey = comm_ctx->mr_local_data[i].ibv->lkey;

    // TODO: Maybe hardcode a wr_id for each communication type.
    memset(&wr[i], 0, sizeof(wr));
    wr[i].wr_id = i;
    wr[i].sg_list = &sge[i];
    wr[i].num_sge = 1;

    // If this is the last write request...
    if (i == comm_ctx->no_local_mrs - 1) {
      wr[i].next = NULL;
    }
    else {
      wr[i].next = &wr[i+1];
    }
  }

  // Indicate that we expect one response per write.
  comm_ctx->events_to_ack = comm_ctx->no_local_mrs;

  // Post the recieve.
  int post_result = ibv_post_recv(comm_ctx->qp, &wr[0], &bad_wr);
  if (post_result) {
    fprintf(stderr, "Posting recieve for data failed with error %d.\n",
      post_result);
    return post_result;
  }

  return 0;
}

int naaice_swnaa_handle_work_completion_data(struct ibv_wc *wc,
  struct naaice_communication_context *comm_ctx) {

  debug_print("In naaice_swnaa_handle_work_completion_data\n");

  // If the work completion status is not success, return with error.
  if (wc->status != IBV_WC_SUCCESS) {
    fprintf(stderr,
            "Status is not IBV_WC_SUCCESS. Status %d for operation %d.\n",
            wc->status, wc->opcode);
    return -1;
  }

  // If we have received a write with immediate (i.e. the last parameter)...
  else if (wc->opcode == IBV_WC_RECV_RDMA_WITH_IMM) {

    // Check if the immediate value is nonzero, indicating an error.
    if (ntohl(wc->imm_data)) {
      fprintf(stderr,
              "Recieved write with nonzero immediate value %d.\n",
              ntohl(wc->imm_data));
      return ntohl(wc->imm_data);
    }

    // Handle the metadata, recording the return address.
    if (naaice_swnaa_handle_metadata(comm_ctx)) {
      fprintf(stderr, "Error during RPC metadata handling.\n");
      return -1;
    }

    // Update the state to indicate RPC is now being executed.
    comm_ctx->state = RUNNING;

    // Indicate that the data has not yet been written back.
    comm_ctx->rdma_writes_done = 0;

    // Now we are ready to perform the NAA procedure.
  }

  // For all other completion codes...
  else {  
    fprintf(stderr,
      "work completion opcode (wc opcode): %d, not handled. Disconnecting.\n",
      wc->opcode);
    return -1;
  }

  // If we've reached here, something has gone wrong.
  fprintf(stderr, "Unknown error while handling work completion for data.\n");
  return -1;
}

int naaice_swnaa_poll_cq_nonblocking_data(
  struct naaice_communication_context *comm_ctx) {

  debug_print("In naaice_swnaa_poll_cq_nonblocking_data\n");

  struct ibv_cq *ev_cq;
  void *ev_ctx;

  // If there is still work to be done...
  if (comm_ctx->events_acked < comm_ctx->events_to_ack) {

    // Ensure completion channel is in non-blocking mode.
    int fd_flags = fcntl(comm_ctx->comp_channel->fd, F_GETFL);
    if (fcntl(comm_ctx->comp_channel->fd, F_SETFL,
          fd_flags | O_NONBLOCK) < 0) {
      fprintf(stderr, "Failed to change file descriptor of completion event "
        "channel.\n");
      return -1;
    }

    struct pollfd my_pollfd;
    int ms_timeout = 100;

    // Poll the completion channel, returning with flag unchanged if nothing
    // is recieved.
    my_pollfd.fd = comm_ctx->comp_channel->fd;
    my_pollfd.events = POLLIN;
    my_pollfd.revents = 0;

    if (poll(&my_pollfd, 1, ms_timeout) < 0) {
      fprintf(stderr, "No completion recieved.\n");
      return 0;
    }

    // If something is recieved, get the completion event.
    if (ibv_get_cq_event(comm_ctx->comp_channel, &ev_cq, &ev_ctx)) {
      fprintf(stderr, "Failed to get completion queue event.\n");
      return -1;
    }
    
    // Ack the completion event.
    ibv_ack_cq_events(ev_cq, comm_ctx->events_to_ack);
    struct ibv_wc wc[comm_ctx->events_to_ack];
    comm_ctx->events_acked = 0;

    // Poll the completion queue.
    int n_wcs = ibv_poll_cq(comm_ctx->cq,
                           comm_ctx->events_to_ack - comm_ctx->events_acked,
                           &wc[comm_ctx->events_acked]);
    // If ibv_poll_cq returns an error, return.
    if (n_wcs < 0) {
      fprintf(stderr, "ibv_poll_cq() failed.\n");
      return -1;
    }

    // Increment the count of acked events.
    comm_ctx->events_acked = comm_ctx->events_acked + n_wcs;
    printf("%d of %d events polled\n", comm_ctx->events_acked,
         comm_ctx->events_to_ack);
  
    // Request completion channel notifications.
    if (ibv_req_notify_cq(comm_ctx->cq, 0)) {
      fprintf(
          stderr,
          "Failed to request completion channel notifications on completion "
          "queue.\n");
      return -1;
    }

    // Handle the work completions.
    // TODO: Handle errors from this. If something goes wrong, disconnect?
    for (int i = 0; i < comm_ctx->events_acked; i++) {
      if (naaice_swnaa_handle_work_completion_data(&wc[i], comm_ctx)) {
        fprintf(stderr, "Error while handling work completion.\n");
        return -1;
      }
    }

    // Check the stopping condition again.
    if (comm_ctx->events_acked < comm_ctx->events_to_ack) {
      comm_ctx->routine_complete = false;
    } else {
      comm_ctx->routine_complete = true;
    }
  }

  // Otherwise we are already done.
  else { comm_ctx->routine_complete = true; }

  return 0;
}

int naaice_swnaa_poll_cq_blocking_data(
  struct naaice_communication_context *comm_ctx) {

  debug_print("In naaice_swnaa_poll_cq_blocking_data\n");

  // TODO: consider a timeout here.
  while(!comm_ctx->routine_complete) {
    if (naaice_swnaa_poll_cq_nonblocking_data(comm_ctx)) { return -1; }
  }

  return 0;
}

int naaice_swnaa_handle_metadata(
  struct naaice_communication_context *comm_ctx) {

  debug_print("In naaice_swnaa_handle_metadata\n");
  
  // Get the return address from the metadata memory region.
  struct naaice_rpc_metadata *rpc_metadata =
      (struct naaice_rpc_metadata*) comm_ctx->mr_local_data[0].addr;
  uintptr_t return_addr = ntohll(rpc_metadata->return_addr);

  // Check that the passed address points to one of the registered regions.
  // Returning to the metadata region (i.e. #0) is not allowed.
  bool flag = false;
  for (int i = 1; i < comm_ctx->no_peer_mrs; i++) {
    if (return_addr == (uintptr_t)comm_ctx->mr_peer_data[i].addr) {
      flag = true;

      // Set this field in the communication context to indicate which
      // parameter is the return parameter.
      comm_ctx->mr_return_idx = i;
      break;
    }
  }
  if (!flag) {
    fprintf(stderr, "Requested return address is not a registered "
      "parameter.\n");
    return -1;
  }

  return 0;
}

int naaice_swnaa_write_data(struct naaice_communication_context *comm_ctx,
  uint8_t errorcode) {

  debug_print("In naaice_swnaa_write_data\n");

  // If an error occured during the NAA routine computation, send an
  // error message to the host.
  // This error message consists simply of the first byte from the first memory
  // region (the metadata region), because a 0 byte transfer is not possible.
  // The immediate value signifies an error by being nonzero.
  if (errorcode) {

    fprintf(stderr, "Error occured during NAA routine computation: %d.\n",
      errorcode);

    // Construct the write request and scatter/gather elements.
    struct ibv_send_wr wr, *bad_wr = NULL;
    struct ibv_sge sge;
    sge.addr = (uintptr_t)comm_ctx->mr_local_data[0].addr;
    sge.length = 1;
    sge.lkey = comm_ctx->mr_local_data[0].ibv->lkey;

    memset(&wr, 0, sizeof(wr));
    wr.wr_id = 1;
    wr.sg_list = &sge;
    wr.num_sge = 0;
    wr.imm_data = htonl(errorcode);
    wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
    wr.send_flags = IBV_SEND_SOLICITED;

    // TODO: Include multiple return regions in the future?
    wr.wr.rdma.remote_addr = 
      comm_ctx->mr_peer_data[comm_ctx->mr_return_idx].addr;
    wr.wr.rdma.rkey = comm_ctx->mr_peer_data[comm_ctx->mr_return_idx].rkey;
    
    // Indicate that we expect one ack in response.
    comm_ctx->events_to_ack = 1;
    comm_ctx->events_acked = 0;

    // Post the send.
    int post_result = ibv_post_send(comm_ctx->qp, &wr, &bad_wr);
    if (post_result) {
      fprintf(stderr, "Posting send for data write "
        "(while sending error message) failed with error %d.\n",
        post_result);
      return post_result;
    }
  }

  // Otherwise, write back only the return parameter specified by the metadata.
  //
  // The NAA routine should place the return value contiguously in the
  // parameter memory regions, starting with the first non-metadata region.
  // Because the return parameter is one of the parameters, it is guranteed
  // that there will be enough room. However, the return value may be split
  // over multiple memory regions if the first (and second, etc.) region is
  // too small to fit the return value.
  //
  // Therefore the data write may consist of one or multiple write requests
  // (each having exactly one scatter/gather elements): one for each memory
  // region required to fit the return value.
  else {

    // Get size of return value.
    uint32_t return_size =
      comm_ctx->mr_peer_data[comm_ctx->mr_return_idx].size;

    // Use this value to track the total size of consequtive memory regions,
    // in order to see how many are needed to fit the return value.
    uint32_t total_size = 0;

    // Pointer keeping track of where we're writing to in the remote region.
    uint64_t return_addr =
      (uint64_t) comm_ctx->mr_local_data[comm_ctx->mr_return_idx].addr;

    // Lists of write requests and scatter/gather elements, with the maximum
    // number being the number of memory regions.
    struct ibv_send_wr wr[comm_ctx->no_local_mrs];
    struct ibv_send_wr *bad_wr = NULL;
    struct ibv_sge sge[comm_ctx->no_local_mrs];

    // For each memory region...
    for (int i = 0; i < comm_ctx->no_local_mrs; i++) {

      // Start building the write request for this region.
      memset(&wr[i], 0, sizeof(struct ibv_send_wr));
      wr[i].wr_id = i;
      wr[i].sg_list = &sge[i];
      wr[i].num_sge = 1;
      wr[i].wr.rdma.remote_addr = return_addr;
      wr[i].wr.rdma.rkey =
        comm_ctx->mr_peer_data[comm_ctx->mr_return_idx].rkey;
      wr[i].opcode = IBV_WR_RDMA_WRITE;

      // Increment the size of the write, if we include this region.
      total_size += comm_ctx->mr_local_data[i].size;

      // Check if the return value will fit into the write if we include
      // this region.
      if (total_size >= return_size) {

        // If so, make a write request and scatter/gather elements for this
        // region, including only so much of this region is as required to fit
        // the return value.
        sge[i].addr = (uintptr_t) comm_ctx->mr_local_data[i].addr;
        sge[i].length =
          (uint32_t) (comm_ctx->mr_local_data[comm_ctx->mr_return_idx].addr
          + return_size - return_addr);
        sge[i].lkey = comm_ctx->mr_local_data[i].ibv->lkey;

        // Then finish up the write request and post it.
        // The last write request is done with an immediate.
        wr[i].opcode = IBV_WR_RDMA_WRITE;
        wr[i].send_flags = IBV_SEND_SOLICITED;
        wr[i].imm_data = htonl(0);
        wr[i].next = NULL;

        int post_result = ibv_post_send(comm_ctx->qp, &wr[0], &bad_wr);
        if (post_result) {
          fprintf(stderr, "Posting send for data write failed with "
            "error %d.\n", post_result);
          return post_result;
        }

        // If successful, go to the finished state and return.
        comm_ctx->state = FINISHED;
        return 0;
      }

      // If not...
      else {

        // If this is the last memory region and we still don't have enough
        // space for the return value, something has gone wrong.
        if (i == comm_ctx->no_local_mrs - 1) {
          fprintf(stderr, "Unable to fit return value into local memory "
            "regions.\n");
          return -1;
        }

        // Simply make a write request and scatter/gather elements for
        // this region.
        // Writes before the last are done without an immediate.
        wr[i].opcode = IBV_WR_RDMA_WRITE;
        wr[i].next = &wr[i+1];

        // Include the full size of this region.
        sge[i].addr = (uintptr_t) comm_ctx->mr_local_data[i].addr;
        sge[i].length = comm_ctx->mr_local_data[i].ibv->length;
        sge[i].lkey = comm_ctx->mr_local_data[i].ibv->lkey;

        // Increment the point at which we are writing data to the remote.
        return_addr += comm_ctx->mr_local_data[i].size;
      }
    }
  }

  // If we've reached here, something has gone wrong.
  fprintf(stderr, "Unknown error while writing data.\n");
  return -1;
}

int naaice_swnaa_disconnect_and_cleanup(
  struct naaice_communication_context *comm_ctx) {

  debug_print("In naaice_swnaa_disconnect_and_cleanup\n");

  // Can simply call same logic used on the host side here.
  return naaice_disconnect_and_cleanup(comm_ctx);
}
