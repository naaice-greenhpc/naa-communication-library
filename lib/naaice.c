#include <infiniband/verbs.h>
#include <naaice.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
// TODO: Include directives for debugging: If not, don't do (most) print
// statements

//Measurement functionality
struct timespec programm_start_time, init_done_time, mr_exchange_done_time,
    data_exchange_done_time[NUMBER_OF_REPITITIONS + 1];

int naaice_prepare_connection(struct naaice_communication_context *comm_ctx) {
  printf("Setting up structures for connection....\n");
  comm_ctx->pd = ibv_alloc_pd(comm_ctx->ibv_ctx);
  if (comm_ctx->pd == NULL) {
    fprintf(stderr, "Couldn't allocate PD.\n");
    return -1;
  }
  // printf("Protection Domain Allocated.\n");

  comm_ctx->cq = ibv_create_cq(comm_ctx->ibv_ctx, RX_DEPTH + 1, NULL, NULL,
                               0); // comm_ctx->comp_channel
  if (comm_ctx->cq == NULL) {
    fprintf(stderr, "Couldn't create completion queue");
    return -1;
  }

  // printf("Completion Queue Created.\n");
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

  int ret = 0;
  ret = rdma_create_qp(comm_ctx->id, comm_ctx->pd, &init_attr);
  if (ret) {
    printf("%d\n", errno);
    perror("Couldn't create queue pair");
    return -1;
  }

  printf("Queue Pair Created.\n");
  comm_ctx->qp = comm_ctx->id->qp;
  comm_ctx->state = READY;
  comm_ctx->events_to_ack = 0;
  comm_ctx->events_acked = 0;

  // Setup Memory region for send/recv messages (MRSP)
  struct naaice_mr_local *lmr = calloc(1, sizeof(struct naaice_mr_local));
  if (lmr == NULL) {
    fprintf(stderr,
            "Failed to allocate local memory for local memory struct.\n");
    exit(EXIT_FAILURE);
  }
  comm_ctx->mr_local_message = lmr;
  // Allocate Memory for the region
  // TODO: flexible size? Now it's fixed at maximum of a Advertisement+request
  // message
  lmr->addr = calloc(1, MR_SIZE_MR_EXHANGE);
  if (lmr->addr == NULL) {
    fprintf(stderr,
            "Failed to allocate memory for memory region setup protocol.\n");
    exit(EXIT_FAILURE);
  }
  // Register memory region
  lmr->ibv = ibv_reg_mr(comm_ctx->pd, lmr->addr, MR_SIZE_MR_EXHANGE,
                        (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));
  if (lmr->ibv == NULL) {
    fprintf(stderr,
            "Failed to register memory for memory region setup protocol.\n");
    exit(EXIT_FAILURE);
  }
  naaice_post_recv(comm_ctx);

  return 0;
}

int naaice_on_addr_resolved(struct rdma_cm_id *id,
                            struct naaice_communication_context *comm_ctx) {
  printf("Address Resolution successful.\n");
  if (rdma_resolve_route(id, TIMEOUT_IN_MS)) {
    perror("RDMA Route Resolving failed: ");
    return -1;
  }
  // What does this do? for rdma_cm_id no member verbs exists, but if not used
  // like this, pd allocation leads to segfault
  comm_ctx->ibv_ctx = id->verbs;
  if (naaice_prepare_connection(comm_ctx)) {
    return -1;
  }
  return 0;
}
//Only called by client, maybe reflect in name
int naaice_on_route_resolved(struct rdma_cm_id *id) {
  // printf("Route Resolution successful.\n");
  // printf("Setting RDMA connection mananger connection parameters.\n");
  struct rdma_conn_param cm_params;
  memset(&cm_params, 0, sizeof(cm_params));
  cm_params.retry_count = 1;
  cm_params.initiator_depth = 1;
  cm_params.responder_resources = 1;
  cm_params.rnr_retry_count = 6; // 7 would be indefinite
  printf("Connecting...\n");
  if (rdma_connect(id, &cm_params)) {
    perror("RDMA Connection failed: ");
    return -1;
  }
  return 0;
}
//ONly called by server, maybe reflect in name
int naaice_on_connection_requests(
    struct rdma_cm_id *id, struct naaice_communication_context *comm_ctx) {
  int ret = 0;
  comm_ctx->id = id;
  comm_ctx->ibv_ctx = id->verbs;
  if (naaice_prepare_connection(comm_ctx)) {
    return -1;
  }
  // printf("Setting RDMA connection mananger connection parameters.\n");
  struct rdma_conn_param cm_params;
  memset(&cm_params, 0, sizeof(cm_params));
  cm_params.retry_count = 1;
  cm_params.initiator_depth = 1;
  cm_params.responder_resources = 1;
  cm_params.rnr_retry_count = 6; // 7 would be indefinite
  printf("Connecting...\n");
  if (rdma_accept(comm_ctx->id, &cm_params)) {
    perror("RDMA Connection failed: ");
    return -1;
  }

  return ret;
}

int naaice_on_disconnect(struct naaice_communication_context *comm_ctx) {
  //TODO: FRee everything that was allocated. 
  
  printf("Connection disconnected successfully. Cleaning up...\n");
  if (comm_ctx->state > MR_EXCHANGE) {
    free(comm_ctx->mr_peer_data);
  }
  int ret = ibv_dereg_mr(comm_ctx->mr_local_message->ibv);
  if (ret) {
    fprintf(stderr, "Deregestering local message mr failed. Reason %d", ret);
    exit(EXIT_FAILURE);
  }
  free((void *)(comm_ctx->mr_local_message->addr));
  free(comm_ctx->mr_local_message);
  for (int i = 0; i < comm_ctx->no_local_mrs; i++) {
    int ret = ibv_dereg_mr(comm_ctx->mr_local_data[i].ibv);
    if (ret) {
      fprintf(stderr, "Deregestering local data mr failed. Reason %d", ret);
      exit(EXIT_FAILURE);
    }
    free((void *)(comm_ctx->mr_local_data[i].addr));
  }
  free(comm_ctx->mr_local_data);
  ret = ibv_destroy_qp(comm_ctx->qp);
  if (ret) {
    fprintf(stderr, "Destroying QP failed. Reason %d", ret);
    exit(EXIT_FAILURE);
  }
  ret = ibv_destroy_cq(comm_ctx->cq);
  if (ret) {
    fprintf(stderr, "Destroying CQ failed. Reason %d", ret);
    exit(EXIT_FAILURE);
  }
  ret = ibv_dealloc_pd(comm_ctx->pd);
  if (ret) {
    fprintf(stderr, "Deallocating PD failed. Reason %d", ret);
    exit(EXIT_FAILURE);
  }
  return -1;
}
// TODO: Restructuring Code to not have function implementation in
// server.c/client.c led to many very similar functions that only differ in a
// small bit.
int naaice_on_event_server(struct rdma_cm_event *ev,
                           struct naaice_communication_context *comm_ctx) {
  if (ev->event == RDMA_CM_EVENT_ADDR_RESOLVED) {
    return naaice_on_addr_resolved(ev->id, comm_ctx);
  } else if (ev->event == RDMA_CM_EVENT_ADDR_ERROR) {
    fprintf(stderr, "RDMA Address Resolution Failed. Exiting.\n");
    exit(EXIT_FAILURE);
  } else if (ev->event == RDMA_CM_EVENT_ROUTE_RESOLVED) {
    return naaice_on_route_resolved(ev->id);
  } else if (ev->event == RDMA_CM_EVENT_ROUTE_ERROR) {
    fprintf(stderr, "RDMA Route Resolution Failed. Exiting.\n");
    exit(EXIT_FAILURE);
  } else if (ev->event == RDMA_CM_EVENT_CONNECT_REQUEST) {
    printf("Incoming Connection Requests\n");
    return naaice_on_connection_requests(ev->id, comm_ctx);
  } else if (ev->event == RDMA_CM_EVENT_CONNECT_ERROR) {
    fprintf(stderr, "Error During Connection Establishment.\n");
    exit(EXIT_FAILURE);
  } else if (ev->event == RDMA_CM_EVENT_UNREACHABLE) {
    fprintf(stderr, "Remote Peer Unreachable.\n");
    exit(EXIT_FAILURE);
  } else if (ev->event == RDMA_CM_EVENT_REJECTED) {
    fprintf(stderr, "Connection Request Rejected by Peer.\n");
    exit(EXIT_FAILURE);
  } else if (ev->event == RDMA_CM_EVENT_DEVICE_REMOVAL) {
    fprintf(stderr, "RDMA Device Was Removed.\n");
    exit(EXIT_FAILURE);
  } else if (ev->event == RDMA_CM_EVENT_ESTABLISHED) {
    return naaice_on_connection_server(ev->id->context);
  } else if (ev->event == RDMA_CM_EVENT_DISCONNECTED) {
    return naaice_on_disconnect(ev->id->context);
  } else {
    fprintf(stderr, "on_event: %d. Unknown event. Exiting", ev->event);
    exit(EXIT_FAILURE);
  }
  return 0;
}

int naaice_on_event_client(struct rdma_cm_event *ev,
                           struct naaice_communication_context *comm_ctx) {
  if (ev->event == RDMA_CM_EVENT_ADDR_RESOLVED) {
    return naaice_on_addr_resolved(ev->id, comm_ctx);
  } else if (ev->event == RDMA_CM_EVENT_ADDR_ERROR) {
    fprintf(stderr, "RDMA Address Resolution Failed. Exiting.\n");
    exit(EXIT_FAILURE);
  } else if (ev->event == RDMA_CM_EVENT_ROUTE_RESOLVED) {
    return naaice_on_route_resolved(ev->id);
  } else if (ev->event == RDMA_CM_EVENT_ROUTE_ERROR) {
    fprintf(stderr, "RDMA Route Resolution Failed. Exiting.\n");
    exit(EXIT_FAILURE);
  } else if (ev->event == RDMA_CM_EVENT_CONNECT_REQUEST) {
    printf("Incoming Connection Requests\n");
    return naaice_on_connection_requests(ev->id, comm_ctx);
  } else if (ev->event == RDMA_CM_EVENT_CONNECT_ERROR) {
    fprintf(stderr, "Error During Connection Establishment.\n");
    exit(EXIT_FAILURE);
  } else if (ev->event == RDMA_CM_EVENT_UNREACHABLE) {
    fprintf(stderr, "Remote Peer Unreachable.\n");
    exit(EXIT_FAILURE);
  } else if (ev->event == RDMA_CM_EVENT_REJECTED) {
    fprintf(stderr, "Connection Request Rejected by Peer.\n");
    exit(EXIT_FAILURE);
  } else if (ev->event == RDMA_CM_EVENT_DEVICE_REMOVAL) {
    fprintf(stderr, "RDMA Device Was Removed.\n");
    exit(EXIT_FAILURE);
  } else if (ev->event == RDMA_CM_EVENT_ESTABLISHED) {
    return naaice_on_connection_client(ev->id->context);
  } else if (ev->event == RDMA_CM_EVENT_DISCONNECTED) {
    return naaice_on_disconnect(ev->id->context);
  } else {
    fprintf(stderr, "on_event: %d. Unknown event. Exiting", ev->event);
    exit(EXIT_FAILURE);
  }
  return 0;
}

void naaice_poll_cq_client(struct naaice_communication_context *comm_ctx) {
  int exit_loop = 0;
  while (!exit_loop) {
    struct ibv_wc wc[comm_ctx->events_to_ack];
    int num_comp = 0;
    comm_ctx->events_acked = 0;
    // Poll until we have all events we're waiting for
    while (comm_ctx->events_acked < comm_ctx->events_to_ack) {
      while (num_comp == 0) {
        // printf("polling cq\n");
        num_comp = ibv_poll_cq(comm_ctx->cq,
                               comm_ctx->events_to_ack - comm_ctx->events_acked,
                               &wc[comm_ctx->events_acked]);
      }
      if (num_comp < 0) {
        // TODO: This exits the program without telling the remote partner.
        fprintf(stderr, "ibv_poll_cq() failed\n");
        exit(EXIT_FAILURE);
      }
      // printf("test cq completions: %d\n", num_comp);
      comm_ctx->events_acked = comm_ctx->events_acked + num_comp;
      printf("%d of %d events acked\n", comm_ctx->events_acked,
             comm_ctx->events_to_ack);
      num_comp = 0;
    }
    // printf("Got CQ Element for each WR\n");
    for (int i = 0; i < comm_ctx->events_acked; i++) {
      exit_loop = naaice_on_completion_client(&wc[i], comm_ctx);
    }
    comm_ctx->events_acked = 0;
  }
  printf("result:%d\n", exit_loop);
  rdma_disconnect(comm_ctx->id);
}

void naaice_poll_cq_server(struct naaice_communication_context *comm_ctx) {
  int exit_loop = 0;
  while (!exit_loop) {
    struct ibv_wc wc[comm_ctx->events_to_ack];
    int num_comp = 0;
    comm_ctx->events_acked = 0;

    while (comm_ctx->events_acked < comm_ctx->events_to_ack) {

      while (num_comp == 0) {
        // printf("polling cq\n");
        num_comp = ibv_poll_cq(comm_ctx->cq,
                               comm_ctx->events_to_ack - comm_ctx->events_acked,
                               &wc[comm_ctx->events_acked]);
      }
      if (num_comp < 0) {
        fprintf(stderr, "ibv_poll_cq() failed\n");
        exit(EXIT_FAILURE);
      }
      // printf("test cq completions: %d\n", num_comp);
      comm_ctx->events_acked = comm_ctx->events_acked + num_comp;
      // comm_ctx->events_to_ack -= comm_ctx->events_acked;
      printf("%d of %d events acked\n", comm_ctx->events_acked,
             comm_ctx->events_to_ack);
      num_comp = 0;
    }
    // printf("Got CQ Element for each WR\n");
    for (int i = 0; i < comm_ctx->events_acked; i++) {
      exit_loop = naaice_on_completion_server(&wc[i], comm_ctx);
    }
    comm_ctx->events_acked = 0;
  }
  printf("result:%d\n", exit_loop);
  rdma_disconnect(comm_ctx->id);
}

void naaice_send_message(struct naaice_communication_context *comm_ctx,
                         enum message_id message_type, size_t requested_size) {
  uint32_t msg_size = 0;
  struct naaice_mr_hdr *msg =
      (struct naaice_mr_hdr *)comm_ctx->mr_local_message->addr;
  msg_size += sizeof(struct naaice_mr_hdr);
  // printf("MSG size: %u \n", msg_size);
  msg->type = message_type;
  // printf("Message type: %u  ",msg->type);
  // MRSP Messages: Advertisement+Request or Advertisement
  if (message_type == MSG_MR_AAR || message_type == MSG_MR_A) {
    struct naaice_mr_dynamic_hdr *dyn =
        (struct naaice_mr_dynamic_hdr *)(msg + sizeof(struct naaice_mr_hdr));
    msg_size += sizeof(struct naaice_mr_dynamic_hdr);
    // printf("MSG size: %u \n", msg_size);
    dyn->count = comm_ctx->no_advertised_mrs;
    dyn->padding[0] = 0;
    dyn->padding[1] = 0;
    // printf("No of MRs: %u  ", dyn->count);
    struct naaice_mr_advertisement *curr =
        (struct naaice_mr_advertisement *)(msg + sizeof(struct naaice_mr_hdr) +
                                           sizeof(
                                               struct naaice_mr_dynamic_hdr));
    for (int i = 0; i < comm_ctx->no_advertised_mrs; i++) {
      curr->addr = htonll((uintptr_t)comm_ctx->mr_local_data[i].addr);
      curr->size = htonl(comm_ctx->mr_local_data[i].ibv->length);
      curr->rkey = htonl(comm_ctx->mr_local_data[i].ibv->rkey);
      printf("Local MR %d: Addr: %lX, Size: %ld, rkey: %d\n", i + 1,
             (uintptr_t)comm_ctx->mr_local_data[i].addr,
             comm_ctx->mr_local_data[i].ibv->length,
             comm_ctx->mr_local_data[i].ibv->rkey);

      curr = (struct naaice_mr_advertisement
                  *)(msg + sizeof(struct naaice_mr_hdr) +
                     sizeof(struct naaice_mr_dynamic_hdr) +
                     (i + 1) * sizeof(struct naaice_mr_advertisement));
      msg_size += sizeof(struct naaice_mr_advertisement);
      // printf("MSG size: %u \n", msg_size);
    }
  }
  // MRSP Messages: Advertisement+Request or Request
  // TODO: See if Request message is needed at all. Usually within A+R
  if (message_type == MSG_MR_AAR || message_type == MSG_MR_R) {
    struct naaice_mr_request *req =
        (struct naaice_mr_request *)(msg + sizeof(struct naaice_mr_hdr) +
                                     sizeof(struct naaice_mr_dynamic_hdr) +
                                     (comm_ctx->no_local_mrs - 1) *
                                         sizeof(
                                             struct naaice_mr_advertisement));
    req->size = htonll(requested_size);
    printf("reqsize %lu\n", req->size);
    msg_size += sizeof(struct naaice_mr_request);
    // printf("Reqsize %lu  ", ntohll(req->size))2;
  }
  // MRSP Message: Error
  if (message_type == MSG_MR_ERR) {
    struct naaice_mr_error *err =
        (struct naaice_mr_error *)(msg + sizeof(struct naaice_mr_hdr));
    // TODO: Allow different error codes. Now we only have one. Maybe re-use
    // requested_size variable as error code
    err->code = (uint8_t)1;
    msg_size += sizeof(struct naaice_mr_error);
  }
  struct ibv_send_wr wr, *bad_wr = NULL;
  struct ibv_sge sge;

  sge.addr = (uintptr_t)comm_ctx->mr_local_message->addr;
  sge.length = msg_size;
  sge.lkey = comm_ctx->mr_local_message->ibv->lkey;
  memset(&wr, 0, sizeof(wr));
  // set send data
  wr.wr_id = message_type; //(uintptr_t)comm_ctx;
  wr.opcode = IBV_WR_SEND;
  wr.sg_list = &sge;
  wr.num_sge = 1;
  wr.send_flags = IBV_SEND_SIGNALED;
  comm_ctx->events_to_ack = 1;
  comm_ctx->events_acked = 0;

  int ret = ibv_post_send(comm_ctx->qp, &wr, &bad_wr);
  if (ret) {
    fprintf(stderr, "Posting Send WQE for SEND failed: %d\n", ret);
    exit(EXIT_FAILURE);
  }
}

int naaice_write_data_client(struct naaice_communication_context *comm_ctx,
                             uint errorcode) {
  int ret = 0;
  // Error-case: e.g Computation error
  /* So far this is only a test case. We always user RDMA Write after MRSP to
   * communicate errors However, in an error case we only send 1 byte from the
   * first data memory region (0 byte transfer not possible I think. The
   * immediate_data signifies whether an error occured. 0 for success, a
   * positive int for anything else*/
  if (errorcode) {
    struct ibv_send_wr wr, *bad_wr = NULL;
    struct ibv_sge sge;
    enum ibv_wr_opcode opcode;
    sge.addr = (uintptr_t)comm_ctx->mr_local_data[0].addr;
    sge.length = 1;
    sge.lkey = comm_ctx->mr_local_data[0].ibv->lkey;
    memset(&wr, 0, sizeof(wr));
    wr.wr_id = 1;
    wr.sg_list = &sge;
    wr.num_sge = 0;
    opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
    wr.imm_data = htonl(errorcode);
    wr.opcode = opcode;
    wr.wr.rdma.remote_addr = comm_ctx->mr_peer_data[0].addr;
    wr.wr.rdma.rkey = comm_ctx->mr_peer_data[0].rkey;
    comm_ctx->events_to_ack = 1;
    comm_ctx->events_acked = 0;
    ret = ibv_post_send(comm_ctx->qp, &wr, &bad_wr);
    if (ret) {
      fprintf(stderr, "Posting Send WQE for WRITE failed: %d\n", ret);
      return ret;
    }
  }
  // Writing Data
  // TODO: Change this to allow writing from multiple regions to one/multiple.
  // Need to keep track of next writing position, also write metadata (output
  // address and possibly info on paramteres) first
  else {
    //Check which regions are specified to be written during this iteration/repitition
    comm_ctx->sges = 0;
    for (int i = 0; i<=comm_ctx->no_advertised_mrs; i++){
      if (comm_ctx->mr_local_data[i].to_write == true){
        comm_ctx->sges++;
      }
    }
    struct ibv_send_wr wr, *bad_wr = NULL;
    struct ibv_sge sge[comm_ctx->sges];
    //enum ibv_wr_opcode opcode;
    //TODO: Think of variable ID?
    memset(&wr,0,sizeof(wr));
    wr.wr_id = 1;
    wr.sg_list = &sge[0];
    wr.num_sge = comm_ctx->sges;
    wr.wr.rdma.remote_addr = comm_ctx->mr_peer_data[0].addr;
    wr.wr.rdma.rkey = comm_ctx->mr_peer_data[0].rkey;
    wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
    wr.imm_data = htonl(errorcode); // htonl(1);

    // Write metadata first
    // TODO, maybe use dynamic size of metadata here
    sge[0].addr =
        (uintptr_t)comm_ctx->mr_local_data[comm_ctx->no_advertised_mrs].addr;
    sge[0].length =
        comm_ctx->mr_local_data[comm_ctx->no_advertised_mrs].ibv->length;
    sge[0].lkey =
        comm_ctx->mr_local_data[comm_ctx->no_advertised_mrs].ibv->lkey;

    // Write all Data regions that are to be written
    bool found;
    for (int i = 1; i < (comm_ctx->sges); i++) {
      for(int j = 0; j < comm_ctx->no_advertised_mrs; j++){
        if (comm_ctx->mr_local_data[j].to_write == true) {
          sge[i].addr = (uintptr_t)comm_ctx->mr_local_data[j].addr;
          sge[i].length = comm_ctx->mr_local_data[j].ibv->length;
          sge[i].lkey = comm_ctx->mr_local_data[j].ibv->lkey;
          printf("Writing %u bytes from advertised region %d\n", sge[i].length,j);
          comm_ctx->mr_local_data[j].to_write = false;
          found = true;
        }
        if (found){
          break;
        }
      }
    }
    //TODO: This is fixed right now. Adjust if more than one wr is necessary (multiple regions/>1GB)
    comm_ctx->events_to_ack = 1;//comm_ctx->no_advertised_mrs;
    comm_ctx->events_acked = 0;
    ret = ibv_post_send(comm_ctx->qp, &wr, &bad_wr);
    if (ret) {
      fprintf(stderr, "Posting Send WQE for WRITE failed: %d\n", ret);
      return ret;
    }
  }
  return ret;
}

int naaice_write_data_server(struct naaice_communication_context *comm_ctx,
                             uint errorcode) {
  int ret = 0;
  // Error-case: e.g Computation error
  /* So far this is only a test case. We always user RDMA Write after MRSP to
   * communicate errors However, in an error case we only send 1 byte from the
   * first data memory region (0 byte transfer not possible I think. The
   * immediate_data signifies whether an error occured. 0 for success, a
   * positive int for anything else*/
  if (errorcode) {
    struct ibv_send_wr wr, *bad_wr = NULL;
    struct ibv_sge sge;
    enum ibv_wr_opcode opcode;
    sge.addr = (uintptr_t)comm_ctx->mr_local_data[0].addr;
    sge.length = 1;
    sge.lkey = comm_ctx->mr_local_data[0].ibv->lkey;
    memset(&wr, 0, sizeof(wr));
    wr.wr_id = 1;
    wr.sg_list = &sge;
    //0 sge is empty message
    wr.num_sge = 0;
    opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
    wr.imm_data = htonl(errorcode);
    wr.opcode = opcode;
    //TODO: Include multiple return regions in the future? 
    wr.wr.rdma.remote_addr = comm_ctx->mr_return_data->addr;
    wr.wr.rdma.rkey = comm_ctx->mr_return_data->rkey;
    comm_ctx->events_to_ack = 1;
    comm_ctx->events_acked = 0;
    ret = ibv_post_send(comm_ctx->qp, &wr, &bad_wr);
    if (ret) {
      fprintf(stderr, "Posting Send WQE for WRITE failed: %d\n", ret);
      return ret;
    }
  }
  // Writing Data
  // TODO: Change this to allow writing from multiple regions to one/multiple.
  // Need to keep track of next writing position, also write metadata (output
  // address and possibly info on paramteres) first
  else {
    //Server writes all regions back, does not have metadata region nor any way to specify, what to write back
    struct ibv_send_wr wr, *bad_wr = NULL;
    struct ibv_sge sge[comm_ctx->no_local_mrs];
    // enum ibv_wr_opcode opcode;
    // TODO: Think of variable ID?
    memset(&wr, 0, sizeof(wr));
    wr.wr_id = 1;
    wr.sg_list = &sge[0];
    wr.num_sge = comm_ctx->no_local_mrs;
    wr.wr.rdma.remote_addr = comm_ctx->mr_return_data->addr;
    wr.wr.rdma.rkey = comm_ctx->mr_return_data->rkey;
    wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
    wr.imm_data = htonl(errorcode); // htonl(1);
    //First sge is special. For pingpong we only write data back, not the metadata at the beginning
    //TODO: This is very basic right now. We need to make sure that what we write back isn't bigger than the return region 
    //TODO: With actual computation we probably overwrite the metadata and can start at the beginning of the memory region?
    sge[0].addr = (uintptr_t)comm_ctx->mr_local_data[0].addr + comm_ctx->rpc_metadata.size;
    //Only write back as much as fits
    sge[0].length = comm_ctx->mr_return_data->size;//comm_ctx->mr_local_data[0].ibv->length  - comm_ctx->rpc_metadata.size;
    sge[0].lkey = comm_ctx->mr_local_data[0].ibv->lkey;

    // Write all Data regions
    //TODO: This will not work...WE need to keep track of where to write to and how much to write
    //Not used right now. Only 1 Memory region on the FPGA-side tested
    if(comm_ctx->no_local_mrs > 1){
      for (int i = 1; i < (comm_ctx->no_local_mrs); i++) {
        sge[i].addr = (uintptr_t)comm_ctx->mr_local_data[i].addr;
        sge[i].length = comm_ctx->mr_local_data[i].ibv->length;
        sge[i].lkey = comm_ctx->mr_local_data[i].ibv->lkey;
      }
    }
    comm_ctx->events_to_ack = comm_ctx->no_local_mrs;
    comm_ctx->events_acked = 0;
    ret = ibv_post_send(comm_ctx->qp, &wr, &bad_wr);
    if (ret) {
      fprintf(stderr, "Posting Send WQE for WRITE failed: %d\n", ret);
      return ret;
    }
  }
  return ret;
}

// TODO: We only post receives for the MRSP region. This works for receiving
// write_with_imm into the data regions as well, but might be a bad style.
// Include region to post receive for
int naaice_post_recv(struct naaice_communication_context *comm_ctx) {
  struct ibv_recv_wr wr, *bad_wr = NULL;
  struct ibv_sge sge;
  sge.addr = (uintptr_t)comm_ctx->mr_local_message->addr;
  sge.length = MR_SIZE_MR_EXHANGE;
  sge.lkey = comm_ctx->mr_local_message->ibv->lkey;

  // TODO: Do we need a unique wr_id? Or just set it the same all the time?
  memset(&wr, 0, sizeof(wr));
  wr.wr_id = (uintptr_t)comm_ctx;
  wr.next = NULL;
  wr.sg_list = &sge;
  wr.num_sge = 1;

  // TODO add a list (here only one at a time) of work request to the receive
  // queue of the QP? 
  //TODO 2: Why? We only post one at a time anyways. Also ending communication with non-finished work requests might be erroneous
  comm_ctx->events_to_ack = 1;
  errno = ibv_post_recv(comm_ctx->qp, &wr, &bad_wr);
  if (errno) {
    fprintf(stderr, "Posting a recive: %d\n", errno);
    exit(EXIT_FAILURE);
  }
  printf("Posted Receive\n");
  return errno;
}

int naaice_register_local_mrs(struct naaice_communication_context *comm_ctx) {
  // Register all the regions that were allocated before
  for (int i = 0; i < ((int)comm_ctx->no_local_mrs); i++) {
    comm_ctx->mr_local_data[i].ibv =
        ibv_reg_mr(comm_ctx->pd, comm_ctx->mr_local_data[i].addr,
                   comm_ctx->mr_local_data[i].size,
                   (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));
    if (comm_ctx->mr_local_data[i].ibv == NULL) {
      fprintf(stderr, "Failed to register memory for local memory region.\n");
      return 1;
    } 
    //Set regions to not be written. Is adjusted later, and in higher-level implementation this is done by middleware
    comm_ctx->mr_local_data[i].to_write = false;
  }
  //Metadata region will always be written
  comm_ctx->mr_local_data[comm_ctx->no_local_mrs-1].to_write = true;
  return 0;
}

int naaice_handle_mr_announce(struct naaice_communication_context *comm_ctx) {
  struct naaice_mr_dynamic_hdr *dyn =
      (struct naaice_mr_dynamic_hdr *)(comm_ctx->mr_local_message->addr +
                                       sizeof(struct naaice_mr_hdr));
  // printf("No of MRs: %u  ", dyn->count);
  //How many peer MRs are there?
  comm_ctx->no_peer_mrs = dyn->count;
  // Allocate resources for peer mrs
  struct naaice_mr_peer *peer =
      calloc(comm_ctx->no_peer_mrs, sizeof(struct naaice_mr_peer));
  if (peer == NULL) {
    fprintf(stderr,
            "Failed to allocate memory remote memory region structure.\n");
    return 1;
  }
  
  comm_ctx->mr_peer_data = (struct naaice_mr_peer *)&peer[0];
  struct naaice_mr_advertisement *mr =
      (struct naaice_mr_advertisement *)(comm_ctx->mr_local_message->addr +
                                         sizeof(struct naaice_mr_hdr) +
                                         sizeof(struct naaice_mr_dynamic_hdr));

  // Save peer mr data from MRSP message
  for (int i = 0; i < (dyn->count); i++) {
    peer[i].addr = ntohll(mr->addr);
    peer[i].rkey = ntohl(mr->rkey);
    peer[i].size = ntohl(mr->size);
    printf("Peer MR %d: Addr: %lX, Size: %d, rkey: %d\n", i + 1, (uintptr_t)peer[i].addr,
           peer[i].size, peer[i].rkey);
    mr = (struct naaice_mr_advertisement
              *)(comm_ctx->mr_local_message->addr +
                 (sizeof(struct naaice_mr_hdr) +
                  sizeof(struct naaice_mr_dynamic_hdr) +
                  (i + 1) * sizeof(struct naaice_mr_advertisement)));
  }
  return 0;
}

int naaice_handle_mr_req(struct naaice_communication_context *comm_ctx) {
  //this is an old implementation. See last part of naaice_handle_mr_announce_and_req for current implementation
  //TODO: Move actual implemenation here
  comm_ctx->requested_size = 0;
  /*struct naaice_mr_request *mr_request =
      (struct naaice_mr_request *)comm_ctx->mr_local_message->addr +
      sizeof(struct naaice_mr_hdr) + sizeof(struct naaice_mr_dynamic_hdr);
  
  comm_ctx->requested_size = mr_request->size;
  struct naaice_mr_local *local;
  local = calloc(comm_ctx->no_local_mrs, sizeof(struct naaice_mr_local));
  if (local == NULL) {
    fprintf(stderr,
            "Failed to allocate memory for local memory region structure\n");
    return 1;
  }
  comm_ctx->mr_local_data = local;
  for (int i = 0; i < (comm_ctx->no_local_mrs); i++) {
    posix_memalign((void **)&(comm_ctx->mr_local_data[i].addr),
                   sysconf(_SC_PAGESIZE), comm_ctx->mr_peer_data[i].size);
    if (comm_ctx->mr_local_data[i].addr == NULL) {
      fprintf(stderr,
              "Failed to allocate memory for local memory region buffer\n");
      return 1;
    }
    comm_ctx->mr_local_data[i].ibv =
        ibv_reg_mr(comm_ctx->pd, comm_ctx->mr_local_data[i].addr,
                   comm_ctx->mr_peer_data[i].size,
                   (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));
    if (comm_ctx->mr_local_data[i].ibv == NULL) {
      fprintf(stderr, "Failed to register memory for local memory region.\n");
      return 1;
    }
  }*/
  return 0;
}

//TODO: Rename. This is advertisement and request now.
int naaice_handle_mr_announce_and_req(
    struct naaice_communication_context *comm_ctx) {
  int ret = 0;
  ret = naaice_handle_mr_announce(comm_ctx);
  if (ret) {
    return 1;
  }
  // TODO: Move this into its own funciton. Already exists, but has to be
  // changed Extract requested memory size
  struct naaice_mr_dynamic_hdr *dyn =
      (struct naaice_mr_dynamic_hdr *)(comm_ctx->mr_local_message->addr +
                                       sizeof(struct naaice_mr_hdr));
  struct naaice_mr_request *req =
      (struct naaice_mr_request *)(comm_ctx->mr_local_message->addr +
                                   sizeof(struct naaice_mr_hdr) +
                                   sizeof(struct naaice_mr_dynamic_hdr) +
                                   dyn->count *
                                       sizeof(struct naaice_mr_advertisement));
  comm_ctx->requested_size = ntohll(req->size);
  printf("requested_size %lu\n", ntohll(req->size));
  // Check how many regions are necessary. We do regions up the maximum transfer
  // size before allocating a new one
  if (comm_ctx->requested_size % MAX_TRANSFER_LENGTH == 0) {
    comm_ctx->no_local_mrs = comm_ctx->requested_size / MAX_TRANSFER_LENGTH;
  } else {
    comm_ctx->no_local_mrs = comm_ctx->requested_size / MAX_TRANSFER_LENGTH + 1;
  }
  printf("no. local mrs: %u\n", comm_ctx->no_local_mrs);
  // TODO: This only works, if the client does not receive a request. In our
  // case the client has more memory regions that it does not necessarily
  // advertise
  comm_ctx->no_advertised_mrs = comm_ctx->no_local_mrs;
  // Allocate structure for local data memory regions
  struct naaice_mr_local *local;
  local = calloc(comm_ctx->no_local_mrs, sizeof(struct naaice_mr_local));
  if (local == NULL) {
    fprintf(stderr,
            "Failed to allocate memory for local memory region structure\n");
    return 1;
  }
  // Allocate memory according to the requested size and register the regions
  comm_ctx->mr_local_data = local;
  // Special case: 1 region only. Size will be requested size
  if (comm_ctx->no_local_mrs == 1) {
    posix_memalign((void **)&(comm_ctx->mr_local_data[0].addr),
                   sysconf(_SC_PAGESIZE), comm_ctx->requested_size);
    if (comm_ctx->mr_local_data[0].addr == NULL) {
      fprintf(stderr,
              "Failed to allocate memory for local memory region buffer\n");
      return 1;
    }
    comm_ctx->mr_local_data[0].size = comm_ctx->requested_size;
    comm_ctx->mr_local_data[0].ibv = ibv_reg_mr(
        comm_ctx->pd, comm_ctx->mr_local_data[0].addr, comm_ctx->requested_size,
        (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));
    if (comm_ctx->mr_local_data[0].ibv == NULL) {
      fprintf(stderr, "Failed to register memory for local memory region.\n");
      return 1;
    }
  }
  // General case: If we need N regions, the first N-1 regions will be the
  // maximum transfer size
  else {
    for (int i = 0; i < (comm_ctx->no_local_mrs); i++) {
      posix_memalign((void **)&(comm_ctx->mr_local_data[i].addr),
                     sysconf(_SC_PAGESIZE), MAX_TRANSFER_LENGTH);
      if (comm_ctx->mr_local_data[i].addr == NULL) {
        fprintf(stderr,
                "Failed to allocate memory for local memory region buffer\n");
        return 1;
      }
      comm_ctx->mr_local_data[i].size = MAX_TRANSFER_LENGTH;
      comm_ctx->mr_local_data[i].ibv = ibv_reg_mr(
          comm_ctx->pd, comm_ctx->mr_local_data[i].addr, MAX_TRANSFER_LENGTH,
          (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));
      if (comm_ctx->mr_local_data[i].ibv == NULL) {
        fprintf(stderr, "Failed to register memory for local memory region.\n");
        return 1;
      }
    }
    // General case: Last memory region: Size is max_transfer_length if we have
    // a multiple of that requested, otherwise it can be derived by modulo
    if (comm_ctx->requested_size % MAX_TRANSFER_LENGTH == 0) {
      posix_memalign(
          (void **)&(comm_ctx->mr_local_data[comm_ctx->no_local_mrs].addr),
          sysconf(_SC_PAGESIZE), MAX_TRANSFER_LENGTH);
      if (comm_ctx->mr_local_data[comm_ctx->no_local_mrs].addr == NULL) {
        fprintf(stderr,
                "Failed to allocate memory for local memory region buffer\n");
        return 1;
      }
      comm_ctx->mr_local_data[comm_ctx->no_local_mrs].size =
          MAX_TRANSFER_LENGTH;
      comm_ctx->mr_local_data[comm_ctx->no_local_mrs].ibv = ibv_reg_mr(
          comm_ctx->pd, comm_ctx->mr_local_data[comm_ctx->no_local_mrs].addr,
          MAX_TRANSFER_LENGTH,
          (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));
      if (comm_ctx->mr_local_data[comm_ctx->no_local_mrs].ibv == NULL) {
        fprintf(stderr, "Failed to register memory for local memory region.\n");
        return 1;
      }
    } else {
      posix_memalign(
          (void **)&(comm_ctx->mr_local_data[comm_ctx->no_local_mrs].addr),
          sysconf(_SC_PAGESIZE),
          comm_ctx->requested_size % MAX_TRANSFER_LENGTH);
      if (comm_ctx->mr_local_data[comm_ctx->no_local_mrs].addr == NULL) {
        fprintf(stderr,
                "Failed to allocate memory for local memory region buffer\n");
        return 1;
      }
      comm_ctx->mr_local_data[comm_ctx->no_local_mrs].size =
          comm_ctx->requested_size % MAX_TRANSFER_LENGTH;
      comm_ctx->mr_local_data[comm_ctx->no_local_mrs].ibv = ibv_reg_mr(
          comm_ctx->pd, comm_ctx->mr_local_data[comm_ctx->no_local_mrs].addr,
          comm_ctx->requested_size % MAX_TRANSFER_LENGTH,
          (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));
      if (comm_ctx->mr_local_data[comm_ctx->no_local_mrs].ibv == NULL) {
        fprintf(stderr, "Failed to register memory for local memory region.\n");
        return 1;
      }
    }
  }
  return 0;
}

int naaice_on_connection_client(struct naaice_communication_context *comm_ctx) {
  printf("Connection Established\n");
  comm_ctx->state = CONNECTED;
  // Measuring Functionality
  clock_gettime(CLOCK_MONOTONIC_RAW, &init_done_time);
  printf("TIME init done: %.9fs\n",
         timediff(programm_start_time, init_done_time));

  comm_ctx->repititions_completed = 0;
  //Register memory regions
  int result = naaice_register_local_mrs(comm_ctx);
  // result = error
  if (result) {
    naaice_send_message(comm_ctx, MSG_MR_ERR, result);
    return -1;
  }
  //advertise regions and request memory
  naaice_send_message(comm_ctx, MSG_MR_AAR, comm_ctx->requested_size);
  naaice_post_recv(comm_ctx);
  naaice_poll_cq_client(comm_ctx);
  return 0;
}

int naaice_on_connection_server(struct naaice_communication_context *comm_ctx) {
  printf("Connection Established\n");
  comm_ctx->state = CONNECTED;
  comm_ctx->rdma_writes_done = 0;
  comm_ctx->repititions_completed = 0;
  comm_ctx->events_to_ack = 0;
  comm_ctx->events_acked = 0;
  //Wait for first message
  naaice_post_recv(comm_ctx);
  naaice_poll_cq_server(comm_ctx);
  return 0;
}

int naaice_on_completion_client(struct ibv_wc *wc,
                                struct naaice_communication_context *comm_ctx) {
  //Error Case
  if (wc->status != IBV_WC_SUCCESS) {
    fprintf(stderr,
            "on_completion: status is not IBV_WC_SUCCESS. Status %d for "
            "operation %d.\n",
            wc->status, wc->opcode);
    return -1;
  } 
  //Error in MRSP or MRSP Advertisement by FPGA
  else if (wc->opcode == IBV_WC_RECV) {
    printf("Receive consumed\n");
    if (comm_ctx->state == MR_EXCHANGE) {
      struct naaice_mr_hdr *msg =
          (struct naaice_mr_hdr *)comm_ctx->mr_local_message->addr;
      // MR advertisement by FPGA, save info on MRS and write data for the first time.
      if (msg->type == MSG_MR_A) {
        // printf("Received Memory region request and advertisement\n");
        struct naaice_mr_dynamic_hdr *dyn =
            (struct naaice_mr_dynamic_hdr *)(comm_ctx->mr_local_message->addr +
                                             sizeof(struct naaice_mr_hdr));
        // printf("No of MRs: %u  ", dyn->count);
        comm_ctx->no_peer_mrs = dyn->count;
        // TODO: This is redudant from server side. We can probably reuse
        // already existing functionality.
        struct naaice_mr_peer *peer =
            calloc(comm_ctx->no_peer_mrs, sizeof(struct naaice_mr_peer));
        if (peer == NULL) {
          fprintf(
              stderr,
              "Failed to allocate memory remote memory region structure.\n");
          naaice_send_message(comm_ctx, MSG_MR_ERR, 1);
          return -1;
        }
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
          printf("Peer MR %d: Addr: %lX, Size: %d, rkey: %d\n", i + 1,
                 peer[i].addr, peer[i].size, peer[i].rkey);
          mr = (struct naaice_mr_advertisement
                    *)(comm_ctx->mr_local_message->addr +
                       (sizeof(struct naaice_mr_hdr) +
                        sizeof(struct naaice_mr_dynamic_hdr) +
                        (i + 1) * sizeof(struct naaice_mr_advertisement)));
        }
        // Measuring functionality
        clock_gettime(CLOCK_MONOTONIC_RAW, &mr_exchange_done_time);
        printf("TIME MR Exchange: %.9fs\n",
               timediff(init_done_time, mr_exchange_done_time));

        // TODO: This is basically the calculation part in case of server or the
        // set_input part of the Client. For client this could done before
        // connection/before MRSP
        printf("MRs set up, doing RMDA_WRITE. Preparing Data...\n");
        for (int i = 0; i < (comm_ctx->no_local_mrs-1); i++) {
          uint32_t *array = (uint32_t *)comm_ctx->mr_local_data[i].addr;
          memset(array, INT_DATA_SUBSTITUTE,
                 comm_ctx->mr_local_data[i].ibv->length);
          // for (uint64_t j = 0; j < comm_ctx->mr_local_data[i].ibv->length /
          // 4; j++) {
          //   // array[j] = htobe32((uint32_t)(j*2%UINT32_MAX));
          //   // printf("%d ",be32toh(array[j]));
          //   array[j] = htobe32((uint32_t)INT_DATA_SUBSTITUTE);
          // }
          // printf("\n");
        }
        printf("Sending data. Array(s) filled with %d's, expecting back the "
               "same incremented by one\n",
               INT_DATA_SUBSTITUTE);
        // Measuring Functionality
        clock_gettime(CLOCK_MONOTONIC_RAW, &data_exchange_done_time[0]);

        //Configure return address and metdata for RPC
        //In this example, we iterate over all regions and write one of them in each repition
        int ret = naaice_configure_metadata(
            comm_ctx,
            //(uintptr_t)comm_ctx->mr_local_data[0].addr);
            //Swith return region, always the one after the one we have written from
            (uintptr_t)comm_ctx->mr_local_data[(comm_ctx->repititions_completed + 1)%comm_ctx->no_advertised_mrs].addr);
        if (ret) {
          fprintf(stderr, "Error in setting RPC metadata, disconnecting\n");
          return 1;
        }
        //Set which regions to write
        //IN this exmaple, we pick the region after the one we're writing from to be the return region
        comm_ctx->mr_local_data[comm_ctx->repititions_completed % (comm_ctx->no_advertised_mrs)]
            .to_write = true;
        //Write Data to FPGA
        ret = naaice_write_data_client(comm_ctx, 0);
        if (ret) {
          comm_ctx->state = FINISHED;
        }
        //Unsetting Write request for memory regions until next round
        comm_ctx
            ->mr_local_data[comm_ctx->repititions_completed %
                            (comm_ctx->no_advertised_mrs)]
            .to_write = false;

        return ret;
      }
      // Tell user the remote error
      else if (msg->type == MSG_MR_ERR) {
        struct naaice_mr_error *err =
            (struct naaice_mr_error *)(comm_ctx->mr_local_message->addr +
                                       sizeof(struct naaice_mr_hdr));
        fprintf(stderr,
                "Remote node encountered error in message exchange: %x\n",
                err->code);
        return -1;
      }
    }
    return 0;
  } 
  //Received Data
  else if (wc->opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
    printf("RDMA WRITE WITH IMM RECEIVED\n");
    printf("Receive consumed\n");
    //Check whether an error occured
    if (ntohl(wc->imm_data)) {
      fprintf(stderr,
              "Immediate value non-zero. Error: %d."
              "Disconnecting.\n",
              ntohl(wc->imm_data));
      return ntohl(wc->imm_data);
    }
    comm_ctx->repititions_completed++;
    printf("Finished repitition nr. %d\n", comm_ctx->repititions_completed);
    // Measuring Functionality
    clock_gettime(CLOCK_MONOTONIC_RAW,
                  &data_exchange_done_time[comm_ctx->repititions_completed]);
    printf("TIME repitition nr. %d: %.9fs\n", comm_ctx->repititions_completed,
        timediff(data_exchange_done_time[comm_ctx->repititions_completed - 1],
                 data_exchange_done_time[comm_ctx->repititions_completed]));

    //are we done with all repititions? Then check data
    if (comm_ctx->repititions_completed == NUMBER_OF_REPITITIONS) {
      printf("Checking received data...\n");
      // Check data if it's correct
      // TODO Is this needed? Only in case of ping-pong example
      for (int i = 0; i < (comm_ctx->no_advertised_mrs-1); i++) {
        uint32_t *array = (uint32_t *)comm_ctx->mr_local_data[i].addr;
        if (!memvcmp(array, INT_DATA_SUBSTITUTE,
                     comm_ctx->mr_local_data[i].ibv->length)) {
          printf("ERROR: Memory Regions don't match!\n");
          printf("Notice, this hasn't been adjusted yet and doesn't work for the updated communication API if the different regions have a different size\n");
        }
      }
      printf("Check done.\n");

      comm_ctx->state = FINISHED;
      printf("All repititions Finished. Completed. Disconnecting\n");
      return 1;
    }
    //Not done, sent data again
    printf("Sending data.\n");
    //How many writes have been done in this repition (only necessary, if multiple work requests are needed)
    comm_ctx->rdma_writes_done = 0;
    //int ret = naaice_configure_metadata(
    //    comm_ctx,
    //     (uintptr_t)comm_ctx->mr_local_data[0].addr);
    
    //Set metadata and return address
    //In this exmaple, each repitition, a different region is used as return region for the FPGA    
    int ret = naaice_configure_metadata(
        comm_ctx,
        //(uintptr_t)comm_ctx->mr_local_data[0].addr);
        // Swith return region, always the one after the one we have written
        // from
        (uintptr_t)comm_ctx
            ->mr_local_data[(comm_ctx->repititions_completed + 1) %
                            comm_ctx->no_advertised_mrs]
            .addr);

    if (ret) {
      fprintf(stderr, "Error in setting RPC metadata, disconnecting\n");
      return 1;
    }
    //Set which region to write 
    //Iterate over all input regions 
    comm_ctx->mr_local_data[comm_ctx->repititions_completed %(comm_ctx->no_advertised_mrs)].to_write = true;
    // Write data
    ret = naaice_write_data_client(comm_ctx, 0);
    if (ret) {
      fprintf(stderr,"Error in writing data\n");
      comm_ctx->state = FINISHED;
    }
    // Unsetting Write
    comm_ctx
        ->mr_local_data[comm_ctx->repititions_completed %
                        (comm_ctx->no_advertised_mrs)]
        .to_write = false;
    return ret;
  } 
  //We have done our part of the MRSP
  else if (wc->opcode == IBV_WC_SEND) {
    printf("sent out all information for every memory region. Waiting on "
           "advertisement of peer memory regions\n");
    comm_ctx->state = MR_EXCHANGE;
    return 0;
  } 
  //Write operation done
  else if (wc->opcode == IBV_WC_RDMA_WRITE) {
    if (comm_ctx->state == FINISHED) {
      printf("Finished. Disconnecting");
      return -1;
    } /*printf("RDMA write completed successfully.");
 printf("opcode %d", wc->opcode);
 printf(" wr id: %ld ", wc->wr_id);*/
    comm_ctx->rdma_writes_done++;
    //TODO this is disabled for now since we only have 1 WR. In the future we have to enable this again and change it.
    //With this variable we want to keep track if all write requests have finished. The number of write requests will no longer equal 
    //the number of advertised MRs, but rather the number of peer data mrs.
    //if (comm_ctx->rdma_writes_done == comm_ctx->no_advertised_mrs) {
      comm_ctx->state = WAIT_DATA;
      naaice_post_recv(comm_ctx);
      printf("\nAll RDMA writes completed successfully. Waiting for results\n");
    //}
    // printf("Writing more\n");
    return 0;
  }
  // All other completion codes...
  fprintf(
      stderr,
      "work completion opcode (wc opcode): %d, not handled. Disconnecting.\n",
      wc->opcode);
  return -1;
}

int naaice_on_completion_server(struct ibv_wc *wc,
                                struct naaice_communication_context *comm_ctx) {

  if (wc->status != IBV_WC_SUCCESS) {
    fprintf(stderr,
            "on_completion: status is not IBV_WC_SUCCESS. Status %d for "
            "operation %d.\n",
            wc->status, wc->opcode);
    return -1;
  }

  if (wc->opcode == IBV_WC_RECV) {
    // the fpga is waiting to receive mrs
    printf("Receive Consumed\n");
    if (comm_ctx->state == CONNECTED) {
      struct naaice_mr_hdr *msg =
          (struct naaice_mr_hdr *)comm_ctx->mr_local_message->addr;
      //Probably no longer necessary, host will always use a+r message
      if (msg->type == MSG_MR_A) {
        printf("Received Memory region advertisement.\n");
        naaice_handle_mr_announce(comm_ctx);
        naaice_post_recv(comm_ctx);
        return 0;
      } else if (msg->type == MSG_MR_AAR) {
        printf("Received Memory region request and advertisement. Preparing "
               "own advertisement\n");
        int ret = 0;
        ret = naaice_handle_mr_announce_and_req(comm_ctx);
        if (ret) {
          naaice_send_message(comm_ctx, MSG_MR_ERR, 1);
          return -1;
        }
        naaice_send_message(comm_ctx, MSG_MR_A, 0);
        naaice_post_recv(comm_ctx);
        return 0;
      } 
      //Handle error case
      else if (msg->type == MSG_MR_ERR) {
        struct naaice_mr_error *err =
            (struct naaice_mr_error *)(comm_ctx->mr_local_message->addr +
                                       sizeof(struct naaice_mr_error));
        fprintf(stderr,
                "Remote node encountered error in message exchange: %d\n",
                err->code);
        return -1;
      }
      return 0;
    }
  } 
  //Received data from host
  else if (wc->opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
    printf("RDMA WRITE WITH IMM RECEIVED\n");
    printf("Receive Consumed\n");
    // printf("Doing Calculations...\n");
    //Check for error. Highly unlikely...
    if (ntohl(wc->imm_data)) {
      fprintf(stderr,
              "Immediate value non-zero. Error: %d."
              "Disconnecting.\n",
              ntohl(wc->imm_data));
      return ntohl(wc->imm_data);
    }
    comm_ctx->state = RUNNING;
    // TODO: This is the calculation part in the future
    printf("Responding with results.\n");

    comm_ctx->rdma_writes_done = 0;
    // Test case for error messages
    if (COMP_ERROR) {
      comm_ctx->state = FINISHED;
    }
    //Handle RPC metadata, first part of message. Includes return address
    int ret = naaice_handle_metadata(comm_ctx);
    if (ret) {
      fprintf(stderr, "Error during rpc metadata handling. Disconnecting\n");
      return ret;
    }
    //Write back the data
    ret = naaice_write_data_server(comm_ctx, COMP_ERROR);
    return ret;
  }
  //FPGA-side of MRSP done
  else if (wc->opcode == IBV_WC_SEND) {
    printf(
        "send completed successfully. All MRs exchanged. Waiting for data\n");
    comm_ctx->state = WAIT_DATA;
    return 0;
  }
  //Write completion
  else if (wc->opcode == IBV_WC_RDMA_WRITE) {
    if (comm_ctx->state == FINISHED) {
      printf("Finished. Disconnecting\n");
      return 1;
    }
    comm_ctx->rdma_writes_done++;
    // printf("opcode %d", wc->opcode);
    // printf(" wr id: %ld", wc->wr_id);
    // printf("RDMA write completed successfully.\n");
    //TODO: This isn't correct necessarily. In the future the number of writes will be equal to the number of return regions 
    if (comm_ctx->rdma_writes_done == comm_ctx->no_advertised_mrs) {
      printf("All RDMA writes completed successfully.\n");
      comm_ctx->repititions_completed++;
      printf("Finished Repitition nr. %d\n", comm_ctx->repititions_completed);
      if (comm_ctx->repititions_completed == NUMBER_OF_REPITITIONS) {
        comm_ctx->state = FINISHED;
        printf("All repititions Finished. Completed. Disconnecting\n");
        return -1;
      }
      naaice_post_recv(comm_ctx);
    }
    return 0;
  }
  //All other completion codes...
  fprintf(
      stderr,
      "work completion opcode (wc opcode): %d, not handled. Disconnecting.\n",
      wc->opcode);
  return -1;
}

// Measuring functionality
double timediff(struct timespec start, struct timespec end) {
  return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
}
// Checking whether region holds the right data in the end...
int memvcmp(void *memory, unsigned char val, unsigned int size) {
  unsigned char *mm = (unsigned char *)memory;
  return (*mm == val) && memcmp(mm, mm + 1, size - 1) == 0;
}

int naaice_configure_metadata(struct naaice_communication_context *comm_ctx,
                              uint64_t return_addr) {

  // Check that the passed address points to one of the registered regions.
  bool flag = false;
  for (int i = 0; i < comm_ctx->no_local_mrs - 1; i++) {
    if (return_addr == (uintptr_t)comm_ctx->mr_local_data[i].addr) {
      flag = true;
      break;
    }
  }
  // If it doesn't, return with an error.
  if (!flag) {
    fprintf(stderr, "Requested return address is not found.\n");
    return -1;
  }

  // Construct the metadata.
  // I guess this is already allocated as part of allocating communication
  // context, so maybe redundant; needed if more than one region
  struct naaice_rpc_metadata *metadata =
      calloc(1, sizeof(struct naaice_rpc_metadata));
  if (metadata == NULL) {
    fprintf(stderr,
            "Failed to allocate local memory for local memory struct.\n");
    exit(EXIT_FAILURE);
  }
  comm_ctx->rpc_metadata = *metadata;
  // TODO: Set other stuff like length, data types etcs.
  // TODO: Adjust length, maybe metadata has length field
  struct naaice_rpc_metadata *curr =
      (struct naaice_rpc_metadata *)(comm_ctx->mr_local_data[comm_ctx->no_advertised_mrs].addr);
  curr->return_addr = htonll((uintptr_t)return_addr);
  //Metdata size is fixed for now....
  curr->size = htonl(comm_ctx->mr_local_data[comm_ctx->no_advertised_mrs].size);
  return 0;
  }

int naaice_handle_metadata(struct naaice_communication_context *comm_ctx) {
  comm_ctx->rpc_metadata.return_addr =  ((uintptr_t)comm_ctx->mr_local_data[0].addr);
  struct naaice_rpc_metadata *metadata =
      (struct naaice_rpc_metadata *)(comm_ctx->mr_local_data[0].addr);
  // Save peer mr data from MRSP message
     comm_ctx->rpc_metadata.return_addr = ntohll(metadata->return_addr);
     comm_ctx->rpc_metadata.size = ntohl(metadata->size);

    bool flag = false;
    //Check if requested return address is one of the already known ones. 
    for (int i = 0; i < comm_ctx->no_peer_mrs; i++) {
      if (comm_ctx->rpc_metadata.return_addr ==
          (uintptr_t)comm_ctx->mr_peer_data[i].addr) {
            printf("Requested return address found within advertised regions\n");
            //Save in a structure with just the return memory regions. Makes it easier in writing data method
            struct naaice_mr_peer *return_mr =
                calloc(1, sizeof(struct naaice_mr_peer));
            if (return_mr == NULL) {
              fprintf(
                  stderr,
                  "Failed to allocate local structure for return address info.\n");
              exit(EXIT_FAILURE);
            }
        comm_ctx->mr_return_data = return_mr;
        comm_ctx->mr_return_data->addr = comm_ctx->mr_peer_data[i].addr;
        comm_ctx->mr_return_data->rkey = comm_ctx->mr_peer_data[i].rkey;
        comm_ctx->mr_return_data->size = comm_ctx->mr_peer_data[i].size;
        flag = true;
        break;
      }
    }
    if (!flag) {
      fprintf(stderr, "Return address is not one of the advertised regions. "
                      "Disconnectin\n");
      // TODO: Specify error code
      return 1;
    }
    return 0;
  }
