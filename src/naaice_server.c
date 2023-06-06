#include <naaice.h>
#include <stdlib.h>
#include <unistd.h>

int naaice_on_completion(struct ibv_wc *wc,
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
      if (msg->type == MSG_MR_A) {
        printf("Received Memory region advertisement.\n");
        naaice_handle_mr_announce(comm_ctx);
        naaice_post_recv(comm_ctx);
        return 0;
      }
      else if (msg->type == MSG_MR_AAR) {
        printf("Received Memory region request and advertisement. Preparing "
               "own advertisement\n");
        int ret = 0;
        ret = naaice_handle_mr_announce_and_req(comm_ctx);
        if(ret){
          naaice_mr_exchange(comm_ctx, MSG_MR_ERR, 1);
          return -1;
        }
        naaice_mr_exchange(comm_ctx, MSG_MR_A, 0);
        naaice_post_recv(comm_ctx);
        return 0;
      } 
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
  } else if (wc->opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
    printf("RDMA WRITE WITH IMM RECEIVED\n");
    printf("Receive Consumed\n");
    // printf("Doing Calculations...\n");
    if (ntohl(wc->imm_data)) {
      fprintf(stderr,
              "Immediate value non-zero. Error: %d."
              "Disconnecting.\n",
              ntohl(wc->imm_data));
      return ntohl(wc->imm_data);
    }
    comm_ctx->state = RUNNING;
    /*printf("immediate data (in network-byte-order by default): %d. Running "
           "calculations\n",
           wc->imm_data);*/
    // struct naaice_mr_local *curr_node;
    // uint32_t recv_int = 0;
    // for (int i = 0; i < (comm_ctx->no_local_mrs); i++) {
    //   curr_node = &comm_ctx->mr_local_data[i];
    //   uint64_t j = 0;
    //   uint32_t *array = (uint32_t *)curr_node->addr;
    //   recv_int = be32toh(array[0]);
    //   // printf("Received Data, ints of value %d\n", recv_int);
    //   for (j = 0; j < curr_node->ibv->length / 4; j++) {
    //     // printf("%d ",be32toh(array[j]));
    //     if (be32toh(array[j]) != recv_int) {
    //       printf("Error in Data transmission");
    //     }
    //     array[j] = htobe32(be32toh(array[j]) + 1);
    //
    //     // printf("value is %d, ",be32toh(array[j]));
    //
    //     // printf("\n");
    //   }
    // }
    printf("Responding with results.\n");

    comm_ctx->rdma_writes_done = 0 ;
    if (COMP_ERROR) {
      comm_ctx->state = FINISHED;
    }
    int ret = naaice_write(comm_ctx,COMP_ERROR);
    return ret;
  }

  else if (wc->opcode == IBV_WC_SEND) {
    printf(
        "send completed successfully. All MRs exchanged. Waiting for data\n");
    comm_ctx->state = WAIT_DATA;
    return 0;
  }

  else if (wc->opcode == IBV_WC_RDMA_WRITE) {
    if(comm_ctx->state == FINISHED){
      printf("Finished. Disconnecting\n");
      return 1;
    }
    comm_ctx->rdma_writes_done++;
    //printf("opcode %d", wc->opcode);
    //printf(" wr id: %ld", wc->wr_id);

    //printf("RDMA write completed successfully.\n");
    if (comm_ctx->rdma_writes_done == comm_ctx->no_local_mrs) {
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

  fprintf(
      stderr,
      "work completion opcode (wc opcode): %d, not handled. Disconnecting.\n",
      wc->opcode);
  return -1;
}

int naaice_on_connection(struct naaice_communication_context *comm_ctx) {
  printf("Connection Established\n");
  comm_ctx->state = CONNECTED;
  comm_ctx->rdma_writes_done = 0;
  comm_ctx->repititions_completed = 0;
  comm_ctx->events_to_ack = 0;
  comm_ctx->events_acked = 0;
  
  naaice_post_recv(comm_ctx);
  naaice_poll_cq(comm_ctx);

  return 0;
}
int main(int argc, __attribute__((unused)) char *argv[]) {
  // TODO maybe make port flexible
  if (argc != 1) {
    fprintf(stderr,
            "Server should be called with no arguments prot is hardcoded :) ");
    exit(EXIT_FAILURE);
  };

  // maybe not on the stack?!
  struct rdma_event_channel *rdma_ev_channel;
  struct rdma_cm_id *rdma_comm_id;
  struct sockaddr loc_addr;
  struct rdma_cm_event *ev = NULL;

  rdma_ev_channel = rdma_create_event_channel();
  if (rdma_ev_channel == NULL) {
    perror("Failed to create rdma event channel");
    exit(EXIT_FAILURE);
  }

  // Third argument is context, null for now
  if (rdma_create_id(rdma_ev_channel, &rdma_comm_id, NULL, RDMA_PS_TCP) == -1) {
    perror("Failed to create a communication id.");
    exit(EXIT_FAILURE);
  }

  memset(&loc_addr, 0, sizeof(loc_addr));
  loc_addr.sa_family = AF_INET;
  ((struct sockaddr_in *)&loc_addr)->sin_port = htons(CONNECTION_PORT);

  if (rdma_bind_addr(rdma_comm_id, &loc_addr)) {
    perror("Binding Communication ID to local address failed. Exiting.\n");
  }

  if (rdma_listen(rdma_comm_id, 10)) { // backlog queue length 10
    perror("Listening on specified port failed.\n");
  }
  char *hello;
  posix_memalign((void **)&(hello), sysconf(_SC_PAGESIZE), 1024);

  int port = ntohs(rdma_get_src_port(rdma_comm_id));
  printf("listening on port %d.\n", port);

  while (rdma_get_cm_event(rdma_ev_channel, &ev) == 0) {
    struct rdma_cm_event ev_cp;
    memcpy(&ev_cp, ev, sizeof(*ev));
    rdma_ack_cm_event(ev);
    if (naaice_on_event(&ev_cp))
      break;
  }
  // clean_id:
  if (rdma_destroy_id(rdma_comm_id) == -1) {
    perror("Failed to destroy RDMA communication id.");
  }
  //printf("Destroyed rdma_comm_id successfully\n");
  rdma_destroy_event_channel(rdma_ev_channel);
  printf("All cleaned up. Exiting\n");
  exit(EXIT_SUCCESS);
  free(hello);
}
