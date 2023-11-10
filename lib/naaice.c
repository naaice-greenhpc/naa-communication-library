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
 * naaice.c
 *
 * Implementations for functions in naaice.h.
 * 
 * Florian Mikolajczak, florian.mikolajczak@uni-potsdam.de
 * Dylan Everingham, everingham@zib.de
 * 
 * 08-11-2023
 * 
 *****************************************************************************/

// Enable debug messages.
#define DEBUG 1

/* Dependencies **************************************************************/

#include <debug.h>
#include <errno.h>
#include <infiniband/verbs.h>
#include <naaice.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>


/* Constants *****************************************************************/

#define TIMEOUT_RESOLVE_ADDR 100
#define CONNECTION_PORT 12345


/* Helper Functions **********************************************************/

// Returns a string representing a work completion opcode.
// Used in debugging.
const char* get_ibv_wc_opcode_str(enum ibv_wc_opcode opcode) {
  switch (opcode) {
    case IBV_WC_SEND: return "IBV_WC_SEND";
    case IBV_WC_RDMA_WRITE: return "IBV_WC_RDMA_WRITE";
    case IBV_WC_RDMA_READ: return "IBV_WC_RDMA_READ";
    case IBV_WC_COMP_SWAP: return "IBV_WC_COMP_SWAP";
    case IBV_WC_FETCH_ADD: return "IBV_WC_FETCH_ADD";
    case IBV_WC_BIND_MW: return "IBV_WC_BIND_MW";
    case IBV_WC_RECV: return "IBV_WC_RECV";
    case IBV_WC_RECV_RDMA_WITH_IMM: return "IBV_WC_RECV_RDMA_WITH_IMM";
    default: return "Unhandled Opcode";
  }
}

// Returns a string representing a naaice connection state.
// Used in debugging.
const char* get_state_str(enum naaice_communication_state state) {
  switch (state) {
    case INIT: return "INIT";
    case READY: return "READY";
    case CONNECTED: return "CONNECTED";
    case MRSP_SENDING: return "MRSP_SENDING";
    case MRSP_RECEIVING: return "MRSP_RECEIVING";
    case MRSP_DONE: return "MRSP_DONE";
    case DATA_SENDING: return "DATA_SENDING";
    case CALCULATING: return "CALCULATING";
    case DATA_RECEIVING: return "DATA_RECEIVING";
    case FINISHED: return "FINISHED";
    case ERROR: return "ERROR";
    default: return "Unknown State";
  }
}


/* Function Implementations **************************************************/

int naaice_init_communication_context(
  struct naaice_communication_context **comm_ctx,
  unsigned int *param_sizes, char **params, unsigned int params_amount,
  uint8_t fncode, const char *local_ip, const char *remote_ip, uint16_t port) {

  debug_print("In naaice_init_communication_context\n");

  // Check function code: should be positive.
  if (fncode < 1) {
    fprintf(stderr, "Function code should be positive.\n");
    return -1;
  }

  // Allocate memory for the communication context.
  *comm_ctx = (struct naaice_communication_context *)
    malloc(sizeof(struct naaice_communication_context));
  if (comm_ctx == NULL) {
    fprintf(stderr, "Failed to allocate memory for communication context.\n");
    return -1;
  }

  // Make an event channel, checking for allocation success.
  (*comm_ctx)->ev_channel = rdma_create_event_channel();
  if (!(*comm_ctx)->ev_channel) {
    fprintf(stderr, "Failed to create an RDMA event channel.\n");
    return -1;
  }

  // Make a communication ID, checking for allocation success.
  struct rdma_cm_id *rdma_comm_id;
  if (rdma_create_id((*comm_ctx)->ev_channel,
                     &rdma_comm_id, NULL, RDMA_PS_TCP) == -1) {
    fprintf(stderr, "Failed to create an RDMA communication id.\n");
    return -1;
  }

  // TODO: This is circular. Why do it like this?
  rdma_comm_id->context = (*comm_ctx);

  // Initialize fields of the communication context.
  (*comm_ctx)->state = INIT;
  (*comm_ctx)->id = rdma_comm_id;
  (*comm_ctx)->no_local_mrs = params_amount + 1;
  (*comm_ctx)->mr_return_idx = 0;
  (*comm_ctx)->rdma_writes_done = 0;
  (*comm_ctx)->fncode = fncode;

  // Initialize local memory region structs.
  struct naaice_mr_local *local;
  local = calloc((*comm_ctx)->no_local_mrs, sizeof(struct naaice_mr_local));
  if (!local) {
    fprintf(stderr,
      "Failed to allocate local memory region structures.\n");
    return -1;
  }
  (*comm_ctx)->mr_local_data = local;

  // First handle all memory regions other than the metadata region.
  for (unsigned int i = 0; i < params_amount; i++) {
    (*comm_ctx)->mr_local_data[i+1].size = param_sizes[i];
    (*comm_ctx)->mr_local_data[i+1].addr = params[i];

    // TODO: consider if this alignment needs to be handled by AP1 or by
    // the user. Currently, it is entirely on the user side.
    /*
    posix_memalign((void **)&(comm_ctx->mr_local_data[i].addr),
           sysconf(_SC_PAGESIZE), comm_ctx->mr_local_data[i].size);
    if (!comm_ctx->mr_local_data[i].addr) {
      fprintf(stderr,
          "Failed to allocate local memory region buffer.\n");
      return -1;
    }
    */
  }

  // Then handle the metadata memory region.
  // We allocate it here, but the actual return address is not known until call
  // to, naaice_set_metadata, where we set it.
  // For now, the size of the metadata is fixed.
  // TODO: handle routine-dependent metadata fields.
  (*comm_ctx)->mr_local_data[0].size = sizeof(struct naaice_rpc_metadata);
  (*comm_ctx)->mr_local_data[0].addr = calloc(1,
                                        sizeof(struct naaice_rpc_metadata));
  if ((*comm_ctx)->mr_local_data[0].addr == NULL) {
    fprintf(stderr,
      "Failed to allocate local memory for metadata memory region.");
    return -1;
  }

  // There is also one additional memory region, where we construct messages
  // sent during MRSP. Currently this is a fixed size region, the size of an
  // advertisement + request message.
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

  // Convert port to string for getaddrinfo.
  char *port_str = malloc(128);
  snprintf(port_str, 128, "%u", port);

  // Get local and remote addresses from getaddrinfo.
  struct addrinfo *rem_addr, *loc_addr;
  if (getaddrinfo(local_ip, port_str, NULL, &loc_addr)) {
    fprintf(stderr, "Failed to get address info for local address.\n");
    return -1;
  }
  if (getaddrinfo(remote_ip, port_str, NULL, &rem_addr)) {
    fprintf(stderr, "Failed to get address info for remote address.\n");
    return -1;
  }

  // Resolve address with communication id, using rdma_resolve_addr.
  if (rdma_resolve_addr(rdma_comm_id, loc_addr->ai_addr, rem_addr->ai_addr,
              TIMEOUT_RESOLVE_ADDR) == -1) {
    fprintf(stderr, "Failed to resolve addresses.\n");
    fprintf(stderr, "errno: %d\n", errno);
    return -1;
  }

  // Addresses no longer needed, call freeaddrinfo on each of them.
  freeaddrinfo(loc_addr);
  freeaddrinfo(rem_addr);

  return 0;
}

int naaice_poll_connection_event(struct naaice_communication_context *comm_ctx,
                                 struct rdma_cm_event *ev,
                                 struct rdma_cm_event *ev_cp) {

  debug_print("In naaice_poll_connection_event\n");

  if (!rdma_get_cm_event(comm_ctx->ev_channel, &ev)) {

    // Acking Event frees memory, therefore copy event before and use it
    // subsequently.
    memcpy(ev_cp, ev, sizeof(*ev));

    // Send an ack in response.
    rdma_ack_cm_event(ev);
    return 0;
  }
  else { return -1; }
}

int naaice_handle_addr_resolved(struct naaice_communication_context *comm_ctx,
  struct rdma_cm_event *ev) {

  debug_print("In naaice_handle_addr_resolved\n");

  if (ev->event == RDMA_CM_EVENT_ADDR_RESOLVED) {

    // Make sure we have not already handled this event...
    if (comm_ctx->state != READY) {

      // Update state.
      comm_ctx->state = READY;

      // FM: This needs to happen before the event for route resolution
      // Dylan: Enforced this using the state machine.
      if (rdma_resolve_route(comm_ctx->id, TIMEOUT_RESOLVE_ROUTE)) {
        fprintf(stderr, "RDMA route resolution failed.\n");
        return -1;
      }

      return naaice_init_rdma_resources(comm_ctx);
    }
  }

  return 0;
}

int naaice_handle_route_resolved(struct naaice_communication_context *comm_ctx,
  struct rdma_cm_event *ev) {

  debug_print("In naaice_handle_route_resolved\n");

  if (ev->event == RDMA_CM_EVENT_ROUTE_RESOLVED) {

    // Make sure that address resolution is complete.
    if (comm_ctx->state != READY) {

      struct rdma_cm_event ev;
      ev.event = RDMA_CM_EVENT_ADDR_RESOLVED;
      naaice_handle_addr_resolved(comm_ctx, &ev);
      // After this, state should be READY.
    }

    // Set connection parameters.
    struct rdma_conn_param cm_params;
    memset(&cm_params, 0, sizeof(cm_params));
    cm_params.retry_count = 1;
    cm_params.initiator_depth = 1;
    cm_params.responder_resources = 1;
    cm_params.rnr_retry_count = 6; // 7 would be indefinite

    if (rdma_connect(comm_ctx->id, &cm_params)) {
      fprintf(stderr, "RDMA connection failed.\n");
      return -1;
    }
  }

  return 0;
}

int naaice_handle_connection_requests(

  // FM: Maybe move this into naaice_swnaa?
  struct naaice_communication_context *comm_ctx,
  struct rdma_cm_event *ev) {

  debug_print("In naaice_handle_connection_requests\n");

  if (ev->event == RDMA_CM_EVENT_CONNECT_REQUEST) {

    // TODO: can unify this with the cm_params used in handle_route_resolved,
    // because all the values are the same.
    struct rdma_conn_param cm_params;
    memset(&cm_params, 0, sizeof(cm_params));
    cm_params.retry_count = 1;
    cm_params.initiator_depth = 1;
    cm_params.responder_resources = 1;
    cm_params.rnr_retry_count = 6; // 7 would be indefinite

    if (naaice_init_rdma_resources(comm_ctx)){
      fprintf(stderr, "Failed in allocating RDMA resources\n");
    }

    // Register the memory region used for MRSP on the server side. 
    comm_ctx->mr_local_message->ibv = ibv_reg_mr(
        comm_ctx->pd, comm_ctx->mr_local_message->addr, MR_SIZE_MR_EXHANGE,
        (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));
    if (comm_ctx->mr_local_message->ibv == NULL) {
      fprintf(stderr,
              "Failed to register memory for memory region setup protocol.\n");
      return -1;
    }

    // FM TODO: We currently have a race condition between client and server. 
    // Server should post receive before accept to circumvent this. Method only
    // available in naaice_swnaa.c.
    // We could put the naaice_handle_connect_requests methods into naaice_swnaa.c
    // maybe? 
    if (rdma_accept(comm_ctx->id, &cm_params)) {
      fprintf(stderr, "RDMA connection failed, in rdma_accept.\n");
      return -1;
    }
  }

  return 0;
}

int naaice_handle_connection_established(
  struct naaice_communication_context *comm_ctx,
  struct rdma_cm_event *ev) {

  debug_print("In naaice_handle_connection_established\n");

  if (ev->event == RDMA_CM_EVENT_ESTABLISHED) {

    // Update state.
    comm_ctx->state = CONNECTED;
  }
  return 0;
}

int naaice_handle_error(struct naaice_communication_context *comm_ctx,
  struct rdma_cm_event *ev) {

  debug_print("In naaice_handle_error\n");

  // Returns -1 and prints an error message if the event is one of
  // the following identified error types.
  // Otherwise returns 0.

  if (ev->event == RDMA_CM_EVENT_ADDR_ERROR) {
    fprintf(stderr, "RDMA address resolution failed.\n");
    return -1;
  }
  else if (ev->event == RDMA_CM_EVENT_ROUTE_ERROR) {
    fprintf(stderr, "RDMA route resolution failed.\n");
    return -1;
  }
  else if (ev->event == RDMA_CM_EVENT_CONNECT_ERROR) {
    fprintf(stderr, "Error during connection establishment.\n");
    return -1;
  }
  else if (ev->event == RDMA_CM_EVENT_UNREACHABLE) {
    fprintf(stderr, "Remote peer unreachable.\n");
    return -1;
  }
  else if (ev->event == RDMA_CM_EVENT_REJECTED) {
    fprintf(stderr, "Connection request rejected by peer.\n");
    return -1;
  }
  else if (ev->event == RDMA_CM_EVENT_DEVICE_REMOVAL) {
    fprintf(stderr, "RDMA device was removed.\n");
    return -1;
  }
  else if (ev->event == RDMA_CM_EVENT_DISCONNECTED) {
    // We're not expecting disconnect at this point, so we should exit.
    fprintf(stderr, "RDMA disconnected unexpectedly.\n");

    // Handle disconnect.
    naaice_disconnect_and_cleanup(comm_ctx);
    return -1;
  }

  return 0;
}

int naaice_handle_other(
  __attribute__((unused)) struct naaice_communication_context *comm_ctx,
  struct rdma_cm_event *ev) {

  debug_print("In naaice_handle_other\n");

  fprintf(stderr, "Unknown event: %d.\n", ev->event);
  return -1;
}

int naaice_poll_and_handle_connection_event(
  struct naaice_communication_context *comm_ctx) {

  debug_print("In naaice_poll_and_handle_connection_event\n");

  // If we've received an event...
  struct rdma_cm_event ev;
  struct rdma_cm_event ev_cp;

  if (!naaice_poll_connection_event(comm_ctx, &ev,&ev_cp)) {
    // FM: TODO:Somehow we always loop through all of them. If we find the right handler, we should not call other handlers
    // Handle possible events.
    // If we fail in handling an event, return.
    // FM many calls using comm_ctx seem to fail if the ibv_ctx of the communication id is not set. 
    // However, any connection event also returns the communication id, so we can just update it here. I don't know if that's a dirty way of doing it though.
    comm_ctx->id = ev_cp.id;
    if (naaice_handle_addr_resolved(comm_ctx, &ev_cp)) {
      return -1;
    }
    if (naaice_handle_route_resolved(comm_ctx, &ev_cp)) {
      return -1;
    }
    if (naaice_handle_connection_requests(comm_ctx, &ev_cp)) {
      return -1;
    }
    if (naaice_handle_connection_established(comm_ctx, &ev_cp)) {
      return -1;
    }
    if (naaice_handle_error(comm_ctx, &ev_cp)) {
      return -1;
    }
    if (naaice_handle_other(comm_ctx, &ev_cp)) {
      return -1;
    }
  }

  // If we sucessfully handled an event (or haven't received one), success.
  return 0;
}

int naaice_setup_connection(struct naaice_communication_context *comm_ctx) {

  debug_print("In naaice_setup_connection\n");

  // Loop handling events and updating the completion flag until finished.
  while (comm_ctx->state < CONNECTED) {

    naaice_poll_and_handle_connection_event(comm_ctx);
  }

  return 0;
}

int naaice_init_rdma_resources(struct naaice_communication_context *comm_ctx){
  // Moved from naaice_handle_addr_resolved since server never encounters this
  // the rdma_cm_id member verbs is only set after rdma_resolve_addr or
  // rdma_resolve_route we could also just pass comm_ctx->id->verbs if thats
  // more well liked
  comm_ctx->ibv_ctx = comm_ctx->id->verbs;

  // Make a protection domain, checking for allocation success.
  debug_print("Making protection domain.\n");
  comm_ctx->pd = ibv_alloc_pd(comm_ctx->ibv_ctx);
  if (comm_ctx->pd == NULL) {
    fprintf(stderr, "Failed to create an RDMA protection domain.\n");
    return -1;
  }

  // Make a completion channel, checking for allocation success.
  debug_print("Making completion channel.\n");
  comm_ctx->comp_channel = ibv_create_comp_channel(comm_ctx->ibv_ctx);
  if (comm_ctx->comp_channel == NULL) {
    fprintf(stderr, "Failed to create an IBV completion channel.\n");
    return -1;
  }

  // Make a completion queue, checking for allocation success.
  debug_print("Making completion queue.\n");
  comm_ctx->cq = ibv_create_cq(comm_ctx->ibv_ctx, RX_DEPTH + 1, NULL,
                               comm_ctx->comp_channel, 0);
  if (comm_ctx->cq == NULL) {
    fprintf(stderr, "Failed to create an IBV completion queue.\n");
    return -1;
  }

  // Request completion queue notifications, checking for success.
  debug_print("Requesting completion queue notifications.\n");
  if (ibv_req_notify_cq(comm_ctx->cq, 0)) {
    fprintf(stderr, "Failed to request completion channel notifications "
                    "on completion queue.\n");
    return -1;
  }

  // Set queue pair attributes.
  struct ibv_qp_init_attr init_attr = {
      .send_cq = comm_ctx->cq,
      .recv_cq = comm_ctx->cq,
      .sq_sig_all = 1,
      // Exceeding the maximum number of wrs (sometimes by a lot more than 1)
      // will lead to an ENOMEM error in ibv_post_send()
      .cap =
          {.max_send_wr = RX_DEPTH,
           .max_recv_wr = RX_DEPTH,
           // TODO: Maybe this needs to be changed if we write from multiple MRs
           .max_send_sge = 32,
           .max_recv_sge = 32},
      // DEFINE COMMUNICATION TYPE!
      .qp_type = IBV_QPT_RC};

  // Make a queue pair, checking for allocation success.
  debug_print("Making queue pair.\n");
  if (rdma_create_qp(comm_ctx->id, comm_ctx->pd, &init_attr)) {
    perror("Failed to create an RDMA queue pair.\n");
    return -1;
  }
  comm_ctx->qp = comm_ctx->id->qp;

  return 0;
}

int naaice_register_mrs(struct naaice_communication_context *comm_ctx) {

  debug_print("In naaice_register_mrs\n");

  // Register all memory regions corresponding to parameters and the metadata
  // memory region.
  for (int i = 0; i < ((int)comm_ctx->no_local_mrs); i++) {

    comm_ctx->mr_local_data[i].ibv =
      ibv_reg_mr(comm_ctx->pd, comm_ctx->mr_local_data[i].addr,
                  comm_ctx->mr_local_data[i].size,
                  (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));

    if (comm_ctx->mr_local_data[i].ibv == NULL) {
      fprintf(stderr, "Failed to register memory for local memory region.\n");
      return -1;
    }
  }

  // Register the memory region used for MRSP.
  comm_ctx->mr_local_message->ibv = 
    ibv_reg_mr(comm_ctx->pd, comm_ctx->mr_local_message->addr,
      MR_SIZE_MR_EXHANGE, (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));
  if (comm_ctx->mr_local_message->ibv == NULL) {
    fprintf(stderr,
            "Failed to register memory for memory region setup protocol.\n");
    return -1;
  }

  return 0;
}

int naaice_set_metadata(struct naaice_communication_context *comm_ctx,
  uintptr_t return_addr) {

  debug_print("In naaice_set_metadata\n");

  // Check that the passed address points to one of the registered regions.
  // Returning to the metadata region (i.e. #0) is not allowed.
  bool flag = false;
  // FM: Why 1 to < comm_ctx->no_local_mrs-1 -> that way we can neither specify
  // the last nor the first mr
  // Dylan: We start with 1 because 0 is the metadata memory region, and
  // returning to that is not allowed. You're right about the upper bound!
  for (int i = 1; i < comm_ctx->no_local_mrs; i++) {
    if (return_addr == (uintptr_t) comm_ctx->mr_local_data[i].addr) {

      // Keep track of which memory region is the return parameter.
      comm_ctx->mr_return_idx = i;
      flag = true;
      break;
    }
  }

  // If it doesn't, return with an error.
  if (!flag) {
    fprintf(stderr, "Requested return address is not a "
      "registered parameter.\n");
    return -1;
  }

  // Metadata region is already allocated in init_communication_context.
  // Therefore, we just need to set the fields.
  // FM: Somehow this doesn't work?
  // Dylan: Seems to be working to me
  struct naaice_rpc_metadata *metadata = 
    (struct naaice_rpc_metadata*) (comm_ctx->mr_local_data[0].addr);
  metadata->return_addr = htonll((uintptr_t)return_addr);

  return 0;
}

int naaice_init_mrsp(struct naaice_communication_context *comm_ctx) {

  debug_print("In naaice_init_mrsp\n");

  // TODO: handle errors from these.
  // Post receive for response before sending message. Otherwise data race.
  // Since we only get back to this level after the send is acknowledged.
  naaice_post_recv_mrsp(comm_ctx);

  // Send MRSP announce + request.
  naaice_send_message(comm_ctx, MSG_MR_AAR, 0);

  return 0;
}

int naaice_init_data_transfer(struct naaice_communication_context *comm_ctx) {

  //FM TODO: Error handling 
  naaice_post_recv_data(comm_ctx);

  naaice_write_data(comm_ctx, comm_ctx->fncode);

  return 0;
}

int naaice_handle_work_completion(struct ibv_wc *wc,
  struct naaice_communication_context *comm_ctx) {

  debug_print("In naaice_handle_work_completion\n");

  debug_print("state: %s, opcode: %s\n",
    get_state_str(comm_ctx->state),
    get_ibv_wc_opcode_str(wc->opcode));
  
  // If the work completion status is not success, return with error.
  if (wc->status != IBV_WC_SUCCESS) {
    fprintf(stderr,
            "Status is not IBV_WC_SUCCESS. Status %d for operation %d.\n",
            wc->status, wc->opcode);
    return -1;
  }

  // If we're still sending the MRSP packet...
  if (comm_ctx->state == MRSP_SENDING) {

    // If we've finished sending the MRSP packet...
    if (wc->opcode == IBV_WC_SEND) {

      // Update state.
      comm_ctx->state = MRSP_RECEIVING;
      return 0;
    }
  }

  // If we're waiting for an MRSP response from the NAA...
  else if (comm_ctx->state == MRSP_RECEIVING) {
  
    // If we have received a write without immediate...
    if (wc->opcode == IBV_WC_RECV) {

      // Then we've recieved an MRSP message. Handle it.

      // Update state.

      struct naaice_mr_hdr *msg =
        (struct naaice_mr_hdr *)comm_ctx->mr_local_message->addr;

      // If the message is a memory region announcment...
      if (msg->type == MSG_MR_A) {

        // printf("Received Memory region request and advertisement\n");
        struct naaice_mr_dynamic_hdr *dyn =
            (struct naaice_mr_dynamic_hdr *)(comm_ctx->mr_local_message->addr +
                                             sizeof(struct naaice_mr_hdr));

        comm_ctx->no_peer_mrs = dyn->count;

        // Allocate memory to store information about remote memory regions.
        struct naaice_mr_peer *peer =
            calloc(comm_ctx->no_peer_mrs, sizeof(struct naaice_mr_peer));
        if (peer == NULL) {
          fprintf(stderr,
              "Failed to allocate memory remote memory region structure.\n");
          
          // If allocation fails, notify remote of failure.
          naaice_send_message(comm_ctx, MSG_MR_ERR, -1);
          return -1;
        }

        // Record information about the NAA's memory regions from the message.
        comm_ctx->mr_peer_data = (struct naaice_mr_peer *)&peer[0];
        struct naaice_mr_advertisement *mr =
            (struct naaice_mr_advertisement
                 *)(comm_ctx->mr_local_message->addr +
                    sizeof(struct naaice_mr_hdr) +
                    sizeof(struct naaice_mr_dynamic_hdr));

        for (int i = 0; i < (dyn->count); i++) {
          peer[i].addr = ntohll(mr->addr);
          peer[i].rkey = ntohl(mr->rkey);
          peer[i].size = ntohl(mr->size);
          debug_print("Peer MR %d: Addr: %lX, Size: %d, rkey: %d\n", i + 1,
                 peer[i].addr, peer[i].size, peer[i].rkey);
          mr = (struct naaice_mr_advertisement
                    *)(comm_ctx->mr_local_message->addr +
                       (sizeof(struct naaice_mr_hdr) +
                        sizeof(struct naaice_mr_dynamic_hdr) +
                        (i + 1) * sizeof(struct naaice_mr_advertisement)));
        }


        // Update state.
        comm_ctx->state = MRSP_DONE;

        return 0;

        // FM TODO: We need to split this up probably. Handling completions
        // should finish after the MRSP is done. Then through some higher-level
        // API we will initiate data transfer. This is necessary to facilitate a
        // semantic split between naa_init() and naa_invoke() from middleware
        // API Write data to the NAA. Go to finished state if successful.

        // Dylan: I'm handling this now using the state machine.
      }

      // Return with error if a remote error has occured.
      else if (msg->type == MSG_MR_ERR) {
        struct naaice_mr_error *err =
            (struct naaice_mr_error *)(comm_ctx->mr_local_message->addr +
                                       sizeof(struct naaice_mr_hdr));
        fprintf(stderr,
                "Remote node encountered error in memory region exchange: "
                "%x.\n",
                err->code);
        return -1;
      }

      // Otherwise, some weird message type. Return with error.
      else {
        fprintf(stderr, "Unhandled MRSP packet type received: %d\n",
          msg->type);
        return -1;
      }
    }
  }

  // If we're sending data...
  else if (comm_ctx->state == DATA_SENDING) {

    // If we've written some data...
    if (wc->opcode == IBV_WC_RDMA_WRITE) {

      // Increment the number of completed writes.
      comm_ctx->rdma_writes_done++;

      // If all writes have been completed, start waiting for the response.
      if (comm_ctx->rdma_writes_done == comm_ctx->no_local_mrs) {

        // Update state.
        // We skip straight to DATA_RECEIVING; the CALCULATING state is used
        // only by the software NAA.
        comm_ctx->state = DATA_RECEIVING;
      }

      return 0;
    }

  }

  // If we're receiving data...
  else if (comm_ctx->state == DATA_RECEIVING) {

    // If we recieved data without an immediate...
    if (wc->opcode == IBV_WC_RECV) {

      // No need to do anything.
      return 0;
    }

    // If we have received data with an immediate...
    if (wc->opcode == IBV_WC_RECV_RDMA_WITH_IMM) {

      // Check whether an error occured.
      if (ntohl(wc->imm_data)) {
        fprintf(stderr,
                "Immediate value non-zero: %d.\n",
                ntohl(wc->imm_data));
        return ntohl(wc->imm_data);
      }

      debug_print("transfer size: %d\n", wc->byte_len);

      // If no error, go to finished state.
      comm_ctx->state = FINISHED;
      return 0;
    }
  }

  // If we've reached this point, the work completion had an opcode which is
  // not handled for the current state, so return with error.
  fprintf(stderr,
      "Work completion opcode (wc opcode): %d, not handled for state:  %d.\n",
      wc->opcode, comm_ctx->state);
  return -1;
}

int naaice_poll_cq_nonblocking(struct naaice_communication_context *comm_ctx) {

  debug_print("In naaice_poll_cq_nonblocking\n");

  struct ibv_cq *ev_cq;
  void *ev_ctx;

  // Ensure completion channel is in non-blocking mode.
  int fd_flags = fcntl(comm_ctx->comp_channel->fd, F_GETFL);
  if (fcntl(comm_ctx->comp_channel->fd, F_SETFL, fd_flags | O_NONBLOCK) < 0) {
    fprintf(stderr, "Failed to change file descriptor of completion event "
      "channel.\n");
    return -1;
  }

  struct pollfd my_pollfd;
  int ms_timeout = 100;
  // Poll the completion channel, returning with flag unchanged if nothing
  // is received.
  my_pollfd.fd = comm_ctx->comp_channel->fd;
  my_pollfd.events = POLLIN;
  my_pollfd.revents = 0;

  // Nonblocking: if poll times out, just return.
  int poll_result = poll(&my_pollfd, 1, ms_timeout);
  if (poll_result < 0) {
    //FM: This is probably an error. If none is received, we get back 0.
    fprintf(stderr, "Error occured when polling completion channel.\n");
    return -1;
  }
  else if (poll_result == 0) {

    // We hav simply not recieved any events.
    return 0;
  }

  // If something is received, get the completion event.
  if (ibv_get_cq_event(comm_ctx->comp_channel, &ev_cq, &ev_ctx)) {
    fprintf(stderr, "Failed to get completion queue event.\n");
    return -1;
  }
  
  // Ack the completion event.
  ibv_ack_cq_events(ev_cq, 1);

  // While there are work completions in the completion queue, handle them.
  struct ibv_wc wc;
  int n_wcs = ibv_poll_cq(comm_ctx->cq, 1, &wc);

  // If ibv_poll_cq returns an error, return.
  if (n_wcs < 0) {
    fprintf(stderr, "ibv_poll_cq() failed.\n");
    return -1;
  }

  while (n_wcs) {

    // Handle the work completion.
    if (naaice_handle_work_completion(&wc, comm_ctx)) {
      fprintf(stderr, "Error while handling work completion.\n");
      return -1;
    }


    // Find any remaining work completions in the queue.
    n_wcs = ibv_poll_cq(comm_ctx->cq, 1, &wc);
    if (n_wcs < 0) {
      fprintf(stderr, "ibv_poll_cq() failed.\n");
      return -1;
    }
  }

  // Request completion channel notifications for the next event.
  if (ibv_req_notify_cq(comm_ctx->cq, 0)) {
    fprintf(
        stderr,
        "Failed to request completion channel notifications on completion "
        "queue.\n");
    return -1;
  }

  return 0;
}

int naaice_do_mrsp(struct naaice_communication_context *comm_ctx) {

  debug_print("In naaice_do_mrsp\n");

  // Initialize the MRSP.
  if (naaice_init_mrsp(comm_ctx)) { return -1; }

  // Poll the completion queue and handle work completions until the MRSP is
  // complete.
  while (comm_ctx->state < MRSP_DONE) {
    if (naaice_poll_cq_nonblocking(comm_ctx)) { return -1; }
  }

  return 0;
}

int naaice_do_data_transfer(struct naaice_communication_context *comm_ctx) {

  debug_print("In naaice_do_data_transfer\n");

  // Initialize the data transfer.
  if (naaice_init_data_transfer(comm_ctx)) { return -1; }

  // Poll the completion queue and handle work completions until data transfer,
  // including sending, calculation, and receiving, is complete.
  while (comm_ctx->state < FINISHED) {
    if (naaice_poll_cq_nonblocking(comm_ctx)) { return -1; }
  }

  return 0;
}

int naaice_disconnect_and_cleanup(
  struct naaice_communication_context *comm_ctx) {

  debug_print("In naaice_disconnect_and_cleanup\n");

  // Disconnect.
  rdma_disconnect(comm_ctx->id);

  // Deregister memory regions.
  int err = 0;
  err = ibv_dereg_mr(comm_ctx->mr_local_message->ibv);
  if (err) {
    fprintf(stderr, "Deregestering local message memory region failed with "
      "error %d.\n", err);
    return -1;
  }
  for (int i = 0; i < comm_ctx->no_local_mrs; i++) {
    err = ibv_dereg_mr(comm_ctx->mr_local_data[i].ibv);
    if (err) {
      fprintf(stderr, "Deregestering local data memory region failed with "
        "error %d.\n", err);
      return -1;
    }
  }

  // Free allocated memory of memory regions.
  // Dylan: only do this for the first memory region (the metadata region) -
  // all the others are parameters and exist in user memory space.
  free((void *)(comm_ctx->mr_local_data[0].addr));

  if (comm_ctx->state >= MRSP_DONE) {
    free(comm_ctx->mr_peer_data);
  }

  free((void *)(comm_ctx->mr_local_message->addr));
  free(comm_ctx->mr_local_message);
  free(comm_ctx->mr_local_data);

  // Destroy queue pair.
  err = ibv_destroy_qp(comm_ctx->qp);
  if (err) {
    fprintf(stderr, "Destroying queue pair failed with error %d.\n", err);
    return -1;
  }

  // Destroy completion queue.
  err = ibv_destroy_cq(comm_ctx->cq);
  if (err) {
    fprintf(stderr, "Destroying completion queue failed with "
      "error %d.\n", err);
    return -1;
  }

  // Destroy completion channel.
  err = ibv_destroy_comp_channel(comm_ctx->comp_channel);
  if (err) {
    fprintf(stderr, "Destroying completion channel failed with "
      "error %d.\n", err);
    return -1;
  }

  // Destroy protection domain.
  err = ibv_dealloc_pd(comm_ctx->pd);
  if (err) {
    fprintf(stderr, "Destroying protection domain failed with "
      "error %d.\n", err);
    return -1;
  }

  // Destroy rdma communication id.
  if (rdma_destroy_id(comm_ctx->id)) {
    perror("Failed to destroy RDMA communication id.\n");
    return -1;
  }

  // Destroy rdma event channel.
  rdma_destroy_event_channel(comm_ctx->ev_channel);

  return 0;
}

int naaice_send_message(struct naaice_communication_context *comm_ctx,
  enum message_id message_type, uint8_t errorcode) {

  debug_print("In naaice_send_message\n");

  // Update state.
  comm_ctx->state = MRSP_SENDING;

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

      // Update packet size.
      msg_size += sizeof(struct naaice_mr_advertisement);
    }
  }

  // If we're sending an advertisement + request message (for MRSP)...
  if(msg->type == MSG_MR_AAR) {

    // Add a dynamic header.
    struct naaice_mr_dynamic_hdr *dyn =
        (struct naaice_mr_dynamic_hdr *)(msg + sizeof(struct naaice_mr_hdr));
    msg_size += sizeof(struct naaice_mr_dynamic_hdr);
    dyn->count = comm_ctx->no_local_mrs;
    dyn->padding[0] = 0;
    dyn->padding[1] = 0;

    // Pointer to the current position in the message being constructed.
    struct naaice_mr_advertisement_request *curr;

    // For each memory region...
    for (int i = 0; i < comm_ctx->no_local_mrs; i++) {

      // Point to next position in the packet.
      curr = (struct naaice_mr_advertisement_request*)
          (msg + sizeof(struct naaice_mr_hdr) +
          sizeof(struct naaice_mr_dynamic_hdr) +
          i * sizeof(struct naaice_mr_advertisement_request));

      // Set fields of the packet.
      // TODO: These flags and fpgaaddresses need to be settable.
      curr->mrflags = 0;
      //FM: Only 7 fields, so <7?
      for(int j = 0; j< 7; j++){
        curr->fpgaaddress[j]= 0;
      }
      curr->addr = htonll((uintptr_t)comm_ctx->mr_local_data[i].addr);
      curr->size = htonl(comm_ctx->mr_local_data[i].ibv->length);
      curr->rkey = htonl(comm_ctx->mr_local_data[i].ibv->rkey);

      
      debug_print("Local MR %d: Addr: %lX, Size: %ld, rkey: %d\n", i + 1,
             (uintptr_t)comm_ctx->mr_local_data[i].addr,
             comm_ctx->mr_local_data[i].ibv->length,
             comm_ctx->mr_local_data[i].ibv->rkey);
      

      // Update packet size.
      msg_size += sizeof(struct naaice_mr_advertisement_request);
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

  // Send the packet.
  int post_result = ibv_post_send(comm_ctx->qp, &wr, &bad_wr);
  if (post_result) {
    fprintf(stderr, "Posting send for MRSP failed with error %d.\n",
      post_result);
    return -1;
  }

  return 0;
}

int naaice_write_data(struct naaice_communication_context *comm_ctx,
  uint8_t fncode) {

  debug_print("In naaice_write_data\n");
  debug_print("fncode: %d\n", fncode);

  // Update state.
  comm_ctx->state = DATA_SENDING;

  // If provided function code is zero, send a write indicated a host-side
  // error. This takes the form of a single byte message with zero
  // immediate value.
  if (fncode == 0) {

    // Construct scatter/gather elements.
    struct ibv_sge sge;
    sge.addr = (uintptr_t)comm_ctx->mr_local_data[0].addr;
    sge.length = 1;
    sge.lkey = comm_ctx->mr_local_data[0].ibv->lkey;

    // Construct write request, which has the scatter/gather elements.
    struct ibv_send_wr wr, *bad_wr = NULL;
    memset(&wr, 0, sizeof(wr));
    wr.wr_id = 1;
    wr.sg_list = &sge;
    wr.num_sge = 0;
    wr.imm_data = htonl(fncode);
    wr.send_flags = IBV_SEND_SOLICITED;
    wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
    wr.wr.rdma.remote_addr = comm_ctx->mr_peer_data[0].addr;
    wr.wr.rdma.rkey = comm_ctx->mr_peer_data[0].rkey;

    // Send the write.
    int post_result = ibv_post_send(comm_ctx->qp, &wr, &bad_wr);
    if (post_result) {
      fprintf(stderr, "Posting send for data write "
        "(while sending error message) failed with error %d.\n",
        post_result);
      return post_result;
    }
  }

  // Otherwise, write all memory regions (metadata + parameters) to NAA.
  // Immediate value is 0, indicating no host-side error.
  // TODO: Change this to allow writing from multiple regions to one/multiple.
  // Need to keep track of next writing position, also write metadata (output
  // address and possibly info on paramteres) first
  else {

    // We will have one write request (and one scatter/gather elements) for
    // each memory region to be written.
    struct ibv_send_wr wr[comm_ctx->no_local_mrs], *bad_wr = NULL;
    struct ibv_sge sge[comm_ctx->no_local_mrs];

    // Construct write requests and scatter/gather elemts for all memory
    // regions to be sent. Metadata region and others can be handled
    // equivalently.
    for (int i = 0; i < comm_ctx->no_local_mrs; i++) {

      memset(&wr[i], 0, sizeof(wr[i]));
      wr[i].wr_id = i+1;
      wr[i].sg_list = &sge[i];
      wr[i].num_sge = 1;
      wr[i].wr.rdma.remote_addr = comm_ctx->mr_peer_data[i].addr;
      wr[i].wr.rdma.rkey = comm_ctx->mr_peer_data[i].rkey;
      wr[i].opcode = IBV_WR_RDMA_WRITE;
      wr[i].next = &wr[i+1];

      // If this is the last memory region to be written, do a write with
      // immediate. The immediate value holds the function code.
      if(i == comm_ctx->no_local_mrs-1) {
        wr[i].opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
        wr[i].send_flags = IBV_SEND_SOLICITED;
        wr[i].imm_data = htonl(fncode);
        wr[i].next = NULL;
      }

      sge[i].addr = (uintptr_t)comm_ctx->mr_local_data[i].addr;
      sge[i].length = comm_ctx->mr_local_data[i].ibv->length;
      sge[i].lkey = comm_ctx->mr_local_data[i].ibv->lkey;
    }

    // Send the write.
    int post_result = ibv_post_send(comm_ctx->qp, &wr[0], &bad_wr);
    if (post_result) {
      fprintf(stderr, "Posting send for data write failed with error %d.\n",
        post_result);
      return post_result;
    }
  }

  return 0;
}

int naaice_post_recv_mrsp(struct naaice_communication_context *comm_ctx) {

  debug_print("In naaice_post_recv_mrsp\n");

  // Construct the receive request and scatter/gather elements.
  struct ibv_recv_wr wr, *bad_wr = NULL;

  struct ibv_sge sge;
  sge.addr = (uintptr_t)comm_ctx->mr_local_message->addr;
  sge.length = MR_SIZE_MR_EXHANGE;
  sge.lkey = comm_ctx->mr_local_message->ibv->lkey;

  // TODO: Maybe hardcode a wr_id for each communication type.
  memset(&wr, 0, sizeof(wr));
  wr.wr_id = 1;
  wr.next = NULL;
  wr.sg_list = &sge;
  wr.num_sge = 1;

  // Post the receive.
  int post_result = ibv_post_recv(comm_ctx->qp, &wr, &bad_wr);
  if (post_result) {
    fprintf(stderr, "Posting receive for MRSP failed with error %d.\n",
      post_result);
    return post_result;
  }

  return 0;
}

int naaice_post_recv_data(struct naaice_communication_context *comm_ctx) {

  debug_print("In naaice_post_recv_data\n");

  // Construct the receive request and scatter/gather elements.
  // We expect a write back to the return parameter memory region.

  struct ibv_recv_wr wr, *bad_wr = NULL;
  struct ibv_sge sge;
  sge.addr = (uintptr_t)comm_ctx->mr_local_data[comm_ctx->mr_return_idx].addr;
  sge.length = comm_ctx->mr_local_data[comm_ctx->mr_return_idx].ibv->length;
  sge.lkey = comm_ctx->mr_local_data[comm_ctx->mr_return_idx].ibv->lkey;

  debug_print("recv addr: %p, length: %d, lkey %d\n",
    (void*) sge.addr, sge.length, sge.lkey);

  // TODO: Maybe hardcode a wr_id for each communication type.
  memset(&wr, 0, sizeof(wr));
  wr.wr_id = 1;
  wr.next = NULL;
  wr.sg_list = &sge;
  wr.num_sge = 1;

  // Post the receive.
  int post_result = ibv_post_recv(comm_ctx->qp, &wr, &bad_wr);
  if (post_result) {
    fprintf(stderr, "Posting receive for data failed with error %d.\n",
      post_result);
    return post_result;
  }

  return 0;
}
