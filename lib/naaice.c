#include <infiniband/verbs.h>
#include <naaice.h>
#include <stdio.h>

int naaice_prepare_connection(struct rdma_cm_id *id) {

  printf("Setting up structures for connection....\n");

  struct naaice_communication_context *comm_ctx =
      (struct naaice_communication_context *)malloc(
          sizeof(struct naaice_communication_context));
  if (comm_ctx == NULL) {
    fprintf(stderr,
            "Failed to allocate memory for communication context. Exiting.");
    return -1;
  }

  id->context = comm_ctx;
  // What does this do? for rdma_cm_id no member verbs exists, but if not used
  // like this, pd allocation leads to segfault
  comm_ctx->ibv_ctx = id->verbs;
  comm_ctx->pd = ibv_alloc_pd(comm_ctx->ibv_ctx);
  if (comm_ctx->pd == NULL) {
    fprintf(stderr, "Couldn't allocate PD.\n");
    return -1;
  }
  // printf("Protection Domain Allocated.\n");
  /*
  comm_ctx->comp_channel = ibv_create_comp_channel(comm_ctx->ibv_ctx);
  if (comm_ctx->comp_channel == NULL) {
    fprintf(stderr, "Couldn't allocate completion channel.\n");
    return -1;
  }
  printf("Completion Channel Created.\n");*/
  comm_ctx->cq = ibv_create_cq(comm_ctx->ibv_ctx, RX_DEPTH + 1, NULL, NULL,
                               0); // comm_ctx->comp_channel
  if (comm_ctx->cq == NULL) {
    fprintf(stderr, "Couldn't create completion queue");
    return -1;
  }

  // printf("Completion Queue Created.\n");
  /*
  if (ibv_req_notify_cq(comm_ctx->cq, 0)) {
    fprintf(stderr,
            "Failed to request completion channel notifications on completion "
            "queue. Exiting.\n");
    return -1;
  }*/

  struct ibv_qp_init_attr init_attr = {
      .send_cq = comm_ctx->cq,
      .recv_cq = comm_ctx->cq,
      .sq_sig_all = 1,
      //Exceeding the maximum number of wrs (sometimes by a lot more than 1) will lead to an ENOMEM error in ibv_post_send()
      .cap = {.max_send_wr = RX_DEPTH,
              .max_recv_wr = RX_DEPTH,
              .max_send_sge = 1,
              .max_recv_sge = 1}, // DEFINE COMMUNICATION TYPE!
      .qp_type = IBV_QPT_RC};
  if (rdma_create_qp(id, comm_ctx->pd, &init_attr)) {
    perror("Couldn't create queue pair");
    return -1;
  }
  printf("Queue Pair Created.\n");
  comm_ctx->qp = id->qp;
  comm_ctx->id = id;
  comm_ctx->state = READY;
  comm_ctx->events_to_ack = 0;
  comm_ctx->events_acked = 0;

  struct naaice_mr_local *lmr = calloc(1, sizeof(struct naaice_mr_local));
  if (lmr == NULL) {
      fprintf(stderr,
              "Failed to allocate local memory for local memory struct.\n");
      exit(EXIT_FAILURE);
  }
  comm_ctx->mr_local_message = lmr;

  // Resources to exchange mrs copied from prepare
  lmr->addr = calloc(1, MR_SIZE_MR_EXHANGE);
  if (lmr->addr == NULL) {
      fprintf(stderr, "Failed to allocate memory for memory region exchange.\n");
      exit(EXIT_FAILURE);
  }

  lmr->ibv = ibv_reg_mr(comm_ctx->pd, lmr->addr, MR_SIZE_MR_EXHANGE,
          (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));
  if (lmr->ibv == NULL) {
      fprintf(stderr, "Failed to allocate memory for memory region exchange.\n");
      exit(EXIT_FAILURE);
  }
  naaice_post_recv(comm_ctx);

  return 0;
}

int naaice_on_addr_resolved(struct rdma_cm_id *id) {
  printf("Address Resolution successful.\n");

  if (rdma_resolve_route(id, TIMEOUT_IN_MS)) {
    perror("RDMA Route Resolving failed: ");
    return -1;
  }
  if (naaice_prepare_connection(id)) {
    return -1;
  }
  return 0;
}

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

int naaice_on_connection_requests(struct rdma_cm_id *id) {
  int ret = 0;
  if (naaice_prepare_connection(id)) {
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
  if (rdma_accept(id, &cm_params)) {
    perror("RDMA Connection failed: ");
    return -1;
  }
  return ret;
}

int naaice_on_disconnect(struct naaice_communication_context *comm_ctx) {
  printf("Connection disconnected successfully. Cleaning up...\n");
  if (comm_ctx->state > MR_EXCHANGE){
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
  /*ret = ibv_destroy_comp_channel(comm_ctx->comp_channel);
  if (ret) {
    fprintf(stderr, "Destroying completion channel failed. Reason %d", ret);
    exit(EXIT_FAILURE);
  }*/
  ret = ibv_dealloc_pd(comm_ctx->pd);
  if (ret) {
    fprintf(stderr, "Deallocating PD failed. Reason %d", ret);
    exit(EXIT_FAILURE);
  }
  return -1; /* exit event loop */
}

int naaice_on_event(struct rdma_cm_event *ev) {
  if (ev->event == RDMA_CM_EVENT_ADDR_RESOLVED) {
    return naaice_on_addr_resolved(ev->id);
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
    return naaice_on_connection_requests(ev->id);
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
    return naaice_on_connection(ev->id->context);
  } else if (ev->event == RDMA_CM_EVENT_DISCONNECTED) {
    return naaice_on_disconnect(ev->id->context);
  } else {
    fprintf(stderr, "on_event: %d. Unknown event. Exiting", ev->event);
    exit(EXIT_FAILURE);
  }
  return 0;
}

void naaice_poll_cq(struct naaice_communication_context *comm_ctx) {
  // struct ibv_cq *cq;
  // void *ctx;

  int result = 0;
  while (!result) {
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
      // DONE: errorhandling

      /* doing acks with events
      if (ibv_get_cq_event(comm_ctx->comp_channel, &cq, &ctx)) {
        fprintf(stderr, "Failed to get cq_event\n");
        exit(EXIT_FAILURE);
      }
      // fprintf(stderr,"ibv_get_cq_event context points to %p",ctx);
      ibv_ack_cq_events(cq, 1);
      comm_ctx->events_acked = comm_ctx->events_acked + 1;
      printf("%d of %d events acked\n", comm_ctx->events_acked,
             comm_ctx->events_to_ack);

      // printf("Event acked \n");
      if (ibv_req_notify_cq(cq, 0)) {
        fprintf(stderr, "Failed to request completion channel notifications on
      " "completion queue. Exiting.\n"); exit(EXIT_FAILURE);
      }*/
    }
    // printf("Got CQ Element for each WR\n");
    for (int i = 0; i < comm_ctx->events_acked; i++) {
      result = naaice_on_completion(&wc[i], comm_ctx);
    }
    comm_ctx->events_acked = 0;
  }
  printf("result:%d\n",result);
  rdma_disconnect(comm_ctx->id);
}

// exchanging memory regions, requested_size can be 0 if message_type !=
// MSG_MR_AAR
void naaice_mr_exchange(struct naaice_communication_context *comm_ctx,
                        enum message_id message_type, size_t requested_size) {
  uint32_t msg_size = 0;
  struct naaice_mr_hdr *msg =
      (struct naaice_mr_hdr *)comm_ctx->mr_local_message->addr;
  msg_size += sizeof(struct naaice_mr_hdr);
  // printf("MSG size: %u \n", msg_size);
  msg->type = message_type;
  // printf("Message type: %u  ",msg->type);
  if (message_type == MSG_MR_AAR || message_type == MSG_MR_A){
    struct naaice_mr_dynamic_hdr *dyn =
          (struct naaice_mr_dynamic_hdr *)(msg + sizeof(struct naaice_mr_hdr));
    msg_size += sizeof(struct naaice_mr_dynamic_hdr);
    // printf("MSG size: %u \n", msg_size);

    dyn->count = comm_ctx->no_local_mrs;
    dyn->padding[0] = 0;
    dyn->padding[1] = 0;
    // printf("No of MRs: %u  ", dyn->count);

    struct naaice_mr_advertisement *curr =
        (struct naaice_mr_advertisement *)(msg + sizeof(struct naaice_mr_hdr) +
                                          sizeof(struct naaice_mr_dynamic_hdr));
    for (int i = 0; i < comm_ctx->no_local_mrs; i++) {
      curr->addr = htonll((uintptr_t)comm_ctx->mr_local_data[i].addr);
      curr->size = htonl(comm_ctx->mr_local_data[i].ibv->length);
      curr->rkey = htonl(comm_ctx->mr_local_data[i].ibv->rkey);
      /* printf("Local MR %d: Addr: %lX, Size: %ld, rkey: %d\n", i + 1,
            (uintptr_t)comm_ctx->mr_local_data[i].addr,
            comm_ctx->mr_local_data[i].ibv->length,
            comm_ctx->mr_local_data[i].ibv->rkey);*/

            curr = (struct naaice_mr_advertisement
                        *)(msg + sizeof(struct naaice_mr_hdr) +
                            sizeof(struct naaice_mr_dynamic_hdr) +
                            (i + 1) * sizeof(struct naaice_mr_advertisement));

            // curr = (curr + sizeof(struct naaice_mr_advertisement));
            msg_size += sizeof(struct naaice_mr_advertisement);
            // printf("MSG size: %u \n", msg_size);
    }
  }
  if (message_type == MSG_MR_AAR || message_type == MSG_MR_R) {
    struct naaice_mr_request *req =
        (struct naaice_mr_request *)(msg + sizeof(struct naaice_mr_hdr) +
                                     sizeof(struct naaice_mr_dynamic_hdr) +
                                     (comm_ctx->no_local_mrs) *
                                         sizeof(
                                             struct naaice_mr_advertisement));
    req->size = htonll(requested_size);
    msg_size += sizeof(struct naaice_mr_request);
    // printf("Reqsize %lu  ", ntohll(req->size))2;
  }
  if (message_type == MSG_MR_ERR){
    struct naaice_mr_error *err =
        (struct naaice_mr_error *)(msg + sizeof(struct naaice_mr_hdr));
    err->code = (uint8_t) 1;
    msg_size += sizeof(struct naaice_mr_error);
  }
  // printf("MSG size: %u \n", msg_size);
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
    fprintf(stderr,"Posting Send WQE for SEND failed: %d\n",ret);
    exit(EXIT_FAILURE);
  }
}

int naaice_write(struct naaice_communication_context *comm_ctx, uint errorcode) {
  int ret = 0;
  if (errorcode){
    struct ibv_send_wr wr, *bad_wr = NULL;
    struct ibv_sge sge;
    enum ibv_wr_opcode opcode;
    sge.addr = (uintptr_t)comm_ctx->mr_local_data[0].addr;
    sge.length = 1;// rdma_comm->peer_size;
    sge.lkey = comm_ctx->mr_local_data[0].ibv->lkey;
    memset(&wr, 0, sizeof(wr));
    wr.wr_id =1;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
    // wr[i].send_flags = IBV_SEND_SIGNALED;
    wr.imm_data = htonl(errorcode); // htonl(1);
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
  else{
    struct ibv_send_wr wr[comm_ctx->no_local_mrs], *bad_wr = NULL;
    struct ibv_sge sge[comm_ctx->no_local_mrs];
    enum ibv_wr_opcode opcode;
    for (int i = 0; i < (comm_ctx->no_local_mrs); i++) {

      sge[i].addr = (uintptr_t)comm_ctx->mr_local_data[i].addr;
      sge[i].length =
          comm_ctx->mr_local_data[i].ibv->length; // rdma_comm->peer_size;
      sge[i].lkey = comm_ctx->mr_local_data[i].ibv->lkey;
      memset(&wr[i], 0, sizeof(wr[i]));
      wr[i].wr_id = i + 1;
      wr[i].sg_list = &sge[i];
      wr[i].num_sge = 1;
      if (i < (comm_ctx->no_local_mrs - 1)) {
        opcode = IBV_WR_RDMA_WRITE;
        if (comm_ctx->no_local_mrs > 1) {
          wr[i].next = &wr[i + 1];
        }
      } else {
        opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
        // wr[i].send_flags = IBV_SEND_SIGNALED;
        wr[i].imm_data = htonl(errorcode); // htonl(1);
      }
      wr[i].opcode = opcode;
      wr[i].wr.rdma.remote_addr = comm_ctx->mr_peer_data[i].addr;
      wr[i].wr.rdma.rkey = comm_ctx->mr_peer_data[i].rkey;
    }
    comm_ctx->events_to_ack = comm_ctx->no_local_mrs;
    comm_ctx->events_acked = 0;
    ret = ibv_post_send(comm_ctx->qp, &wr[0], &bad_wr);
    if (ret) {
      fprintf(stderr, "Posting Send WQE for WRITE failed: %d\n", ret);
      return ret;
    }
  }
  return ret;
  // wr.send_flags = IBV_SEND_SIGNALED;
}

int naaice_post_recv(struct naaice_communication_context *comm_ctx) {
  struct ibv_recv_wr wr, *bad_wr = NULL;

  struct ibv_sge sge;
  sge.addr = (uintptr_t)comm_ctx->mr_local_message->addr;
  sge.length = MR_SIZE_MR_EXHANGE;
  sge.lkey = comm_ctx->mr_local_message->ibv->lkey;

  // set the work request
  memset(&wr, 0, sizeof(wr));
  wr.wr_id = (uintptr_t)comm_ctx;
  wr.next = NULL;
  wr.sg_list = &sge;
  wr.num_sge = 1;

  // add a list (here only one at a time) of work request to the receive queue
  // of the QP
  comm_ctx->events_to_ack = 1;
  errno = ibv_post_recv(comm_ctx->qp, &wr, &bad_wr);
  if (errno) {
    fprintf(stderr,"Posting a recive: %d\n",errno);
    exit(EXIT_FAILURE);
  }
  printf("Posted Receive\n");
  return errno;
}

int naaice_prepare_local_mrs(struct naaice_communication_context *comm_ctx) {
  if (comm_ctx->transfer_length % MAX_TRANSFER_LENGTH == 0) {
    comm_ctx->no_local_mrs = comm_ctx->transfer_length / MAX_TRANSFER_LENGTH;
  } else {
    comm_ctx->no_local_mrs =
        comm_ctx->transfer_length / MAX_TRANSFER_LENGTH + 1;
  }
  printf("no. local mrs: %u\n", comm_ctx->no_local_mrs);

  // Resources to exchange mrs copied from prepare

  struct naaice_mr_local *local;
  local = calloc(comm_ctx->no_local_mrs, sizeof(struct naaice_mr_local));
  if (local == NULL) {
    fprintf(stderr,
            "Failed to allocate memory for local memory region structure\n");
    return 1;
  }
  comm_ctx->mr_local_data = local;
  comm_ctx->rdma_write_finished = 0;
  comm_ctx->rdma_writes_done = 0;
  // Resources to exchange mrs copied from prepare
  if (comm_ctx->no_local_mrs == 1) {
    posix_memalign((void **)&(comm_ctx->mr_local_data[0].addr),
                   sysconf(_SC_PAGESIZE), comm_ctx->transfer_length);
    if (comm_ctx->mr_local_data[0].addr == NULL) {
      fprintf(stderr,
              "Failed to allocate memory for local memory region buffer\n");
      return 1;
    }
    comm_ctx->mr_local_data[0].ibv =
        ibv_reg_mr(comm_ctx->pd, comm_ctx->mr_local_data[0].addr,
                   comm_ctx->transfer_length,
                   (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));
    if (comm_ctx->mr_local_data[0].ibv == NULL) {
      fprintf(stderr, "Failed to register memory for local memory region.\n");
      return 1;
    }
  }
  for (int i = 0; i < ((int)comm_ctx->no_local_mrs - 1); i++) {
    posix_memalign((void **)&(comm_ctx->mr_local_data[i].addr),
                   sysconf(_SC_PAGESIZE), MAX_TRANSFER_LENGTH);
    if (comm_ctx->mr_local_data[i].addr == NULL) {
      fprintf(stderr,
              "Failed to allocate memory for local memory region buffer\n");
      return 1;
    }
    comm_ctx->mr_local_data[i].ibv = ibv_reg_mr(
        comm_ctx->pd, comm_ctx->mr_local_data[i].addr, MAX_TRANSFER_LENGTH,
        (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));
    if (comm_ctx->mr_local_data[i].ibv == NULL) {
      fprintf(stderr, "Failed to register memory for local memory region.\n");
      return 1;
    }
  }
  if (comm_ctx->no_local_mrs > 1) {
    if (comm_ctx->transfer_length % MAX_TRANSFER_LENGTH == 0) {
      posix_memalign(
          (void **)&(comm_ctx->mr_local_data[comm_ctx->no_local_mrs - 1].addr),
          sysconf(_SC_PAGESIZE), MAX_TRANSFER_LENGTH);
      if (comm_ctx->mr_local_data[comm_ctx->no_local_mrs - 1].addr == NULL) {
        fprintf(stderr,
                "Failed to allocate memory for local memory region buffer\n");
        return 1;
      }
      comm_ctx->mr_local_data[comm_ctx->no_local_mrs - 1].ibv =
          ibv_reg_mr(comm_ctx->pd,
                     comm_ctx->mr_local_data[comm_ctx->no_local_mrs - 1].addr,
                     MAX_TRANSFER_LENGTH,
                     (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));
      if (comm_ctx->mr_local_data[comm_ctx->no_local_mrs - 1].ibv == NULL) {
        fprintf(stderr, "Failed to register memory for local memory region.\n");
        return 1;
      }
    //return 0;
    } 
    else {
      posix_memalign(
          (void **)&(comm_ctx->mr_local_data[comm_ctx->no_local_mrs - 1].addr),
          sysconf(_SC_PAGESIZE),
          comm_ctx->transfer_length % MAX_TRANSFER_LENGTH);
      if (comm_ctx->mr_local_data[comm_ctx->no_local_mrs - 1].addr == NULL) {
        fprintf(stderr,
                "Failed to allocate memory for local memory region buffer\n");
        return 1;
      }
      comm_ctx->mr_local_data[comm_ctx->no_local_mrs - 1].ibv =
          ibv_reg_mr(comm_ctx->pd,
                     comm_ctx->mr_local_data[comm_ctx->no_local_mrs - 1].addr,
                     comm_ctx->transfer_length % MAX_TRANSFER_LENGTH,
                     (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));
      if (comm_ctx->mr_local_data[comm_ctx->no_local_mrs - 1].ibv == NULL) {
        fprintf(stderr, "Failed to register memory for local memory region.\n");
        return 1;
      }
    }
  }
  return 0;
}

int naaice_handle_mr_announce(struct naaice_communication_context *comm_ctx) {
  struct naaice_mr_dynamic_hdr *dyn =
      (struct naaice_mr_dynamic_hdr *)(comm_ctx->mr_local_message->addr +
                                       sizeof(struct naaice_mr_hdr));
  // printf("No of MRs: %u  ", dyn->count);
  comm_ctx->no_local_mrs = dyn->count;
  struct naaice_mr_peer *peer =
      calloc(comm_ctx->no_local_mrs, sizeof(struct naaice_mr_peer));
  if (peer == NULL) {
    fprintf(stderr,
            "Failed to allocate memory remote memory region structure.\n");
    return 1;
  }
  struct naaice_mr_local *local;
  local = calloc(comm_ctx->no_local_mrs, sizeof(struct naaice_mr_local));
  if (local == NULL) {
    fprintf(stderr,
            "Failed to allocate memory for local memory region structure\n");
    return 1;
  }
  comm_ctx->mr_peer_data = (struct naaice_mr_peer *)&peer[0];
  struct naaice_mr_advertisement *mr =
      (struct naaice_mr_advertisement *)(comm_ctx->mr_local_message->addr +
                                         sizeof(struct naaice_mr_hdr) +
                                         sizeof(struct naaice_mr_dynamic_hdr));
  for (int i = 0; i < (dyn->count); i++) {
    peer[i].addr = ntohll(mr->addr);
    peer[i].rkey = ntohl(mr->rkey);
    peer[i].size = ntohl(mr->size);
    /*printf("Peer MR %d: Addr: %lX, Size: %d, rkey: %d\n", i + 1, peer[i].addr,
           peer[i].size, peer[i].rkey);*/
    mr = (struct naaice_mr_advertisement
              *)(comm_ctx->mr_local_message->addr +
                 (sizeof(struct naaice_mr_hdr) +
                  sizeof(struct naaice_mr_dynamic_hdr) +
                  (i + 1) * sizeof(struct naaice_mr_advertisement)));
  }
  return 0;
}

int naaice_handle_mr_req(struct naaice_communication_context *comm_ctx) {
  struct naaice_mr_request *mr_request =
      (struct naaice_mr_request *)comm_ctx->mr_local_message->addr +
      sizeof(struct naaice_mr_hdr) + sizeof(struct naaice_mr_dynamic_hdr);
  comm_ctx->transfer_length = mr_request->size;
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
        ibv_reg_mr(comm_ctx->pd, comm_ctx->mr_local_data[i].addr,  comm_ctx->mr_peer_data[i].size,
                   (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));
    if (comm_ctx->mr_local_data[i].ibv == NULL) {
      fprintf(stderr, "Failed to register memory for local memory region.\n");
      return 1;
    }
  }
  return 0;
}


int naaice_handle_mr_announce_and_req(struct naaice_communication_context *comm_ctx) {
  int ret = 0;
  ret = naaice_handle_mr_announce(comm_ctx);
  if(ret){
    return 1;
  }
  struct naaice_mr_dynamic_hdr *dyn =
      (struct naaice_mr_dynamic_hdr *)(comm_ctx->mr_local_message->addr +
                                       sizeof(struct naaice_mr_hdr));
  struct naaice_mr_request *req =
      (struct naaice_mr_request *)(dyn +
                                   dyn->count *
                                       sizeof(struct naaice_mr_advertisement));
  comm_ctx->transfer_length = req->size;
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
        ibv_reg_mr(comm_ctx->pd, comm_ctx->mr_local_data[i].addr,  comm_ctx->mr_peer_data[i].size,
                   (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));
    if (comm_ctx->mr_local_data[i].ibv == NULL) {
      fprintf(stderr, "Failed to register memory for local memory region.\n");
      return 1;
    }
  }
  return 0;
}

