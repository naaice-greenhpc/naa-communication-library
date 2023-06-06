#include <naaice.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#define INT_DATA_SUBSTITUTE 11

char conn_port[6];
static uint64_t transfer_length;

struct timespec programm_start_time, init_done_time, mr_exchange_done_time, data_exchange_done_time[NUMBER_OF_REPITITIONS + 1];

static inline double timediff(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
}

int naaice_on_connection(struct naaice_communication_context *comm_ctx) {
  printf("Connection Established\n");
  comm_ctx->state = CONNECTED;

  clock_gettime(CLOCK_MONOTONIC_RAW, &init_done_time);
  printf("TIME init done: %.9fs\n", timediff(programm_start_time, init_done_time));

  comm_ctx->transfer_length = transfer_length;
  comm_ctx->repititions_completed = 0;

  int result = naaice_prepare_local_mrs(comm_ctx);
  if(result){
    naaice_mr_exchange(comm_ctx, MSG_MR_ERR, result);
    return -1;
  }
  // send first mr. if only, do advertisement and request, else only
  // advertisement. rest will be dealt with in completion of IBV_WC_SEND;
  naaice_mr_exchange(comm_ctx, MSG_MR_AAR, comm_ctx->transfer_length);
  naaice_post_recv(comm_ctx);
  naaice_poll_cq(comm_ctx);

  return 0;
}

static int memvcmp(void *memory, unsigned char val, unsigned int size) {
    unsigned char *mm = (unsigned char*)memory;
    return (*mm == val) && memcmp(mm, mm + 1, size - 1) == 0;
}


int naaice_on_completion(struct ibv_wc *wc,
                         struct naaice_communication_context *comm_ctx) {

  if (wc->status != IBV_WC_SUCCESS) {
    fprintf(stderr,
            "on_completion: status is not IBV_WC_SUCCESS. Status %d for "
            "operation %d.\n",
            wc->status, wc->opcode);
    return -1;
  } 
  else if (wc->opcode == IBV_WC_RECV) {
    printf("Receive consumed\n");
    if (comm_ctx->state == MR_EXCHANGE) {

      struct naaice_mr_hdr *msg =
          (struct naaice_mr_hdr *)comm_ctx->mr_local_message->addr;
      if (msg->type == MSG_MR_A) {
        /*printf("Received Memory region request and advertisement\n");
        struct naaice_mr_dynamic_hdr *dyn_hdr =  (struct naaice_mr_dynamic_hdr
        *)(comm_ctx->mr_local_message->addr+sizeof(struct naaice_mr_hdr));
        uint8_t count = dyn_hdr->count;
        struct naaice_mr_peer *peer = calloc(count, sizeof(struct
        naaice_mr_peer)); if (peer == NULL){ fprintf(stderr,"Failed to allocate
        memory remote memory region structure.\n"); exit(EXIT_FAILURE);
        }
        comm_ctx->mr_peer_data = (struct naaice_mr_peer*) &peer[0];
        struct naaice_mr_advertisement *mr = ( struct naaice_mr_advertisement
        *)(dyn_hdr + sizeof(struct naaice_mr_dynamic_hdr)); for(int i = 0; i <
        (count -1); i++){ peer[i].addr = ntohll(mr->addr); peer[i].rkey =
        ntohl(mr->rkey); peer[i].size = ntohl(mr->size);
        }*/
        // naaice_handle_mr_announce(comm_ctx);
        struct naaice_mr_dynamic_hdr *dyn =
            (struct naaice_mr_dynamic_hdr *)(comm_ctx->mr_local_message->addr +
                                             sizeof(struct naaice_mr_hdr));
        // printf("No of MRs: %u  ", dyn->count);
        comm_ctx->no_local_mrs = dyn->count;
        struct naaice_mr_peer *peer =
            calloc(comm_ctx->no_local_mrs, sizeof(struct naaice_mr_peer));
        if (peer == NULL) {
          fprintf(
              stderr,
              "Failed to allocate memory remote memory region structure.\n");
          naaice_mr_exchange(comm_ctx, MSG_MR_ERR, 1);
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
          /*printf("Peer MR %d: Addr: %lX, Size: %d, rkey: %d\n", i + 1,
                 peer[i].addr, peer[i].size, peer[i].rkey);*/
          mr = (struct naaice_mr_advertisement
                    *)(comm_ctx->mr_local_message->addr +
                       (sizeof(struct naaice_mr_hdr) +
                        sizeof(struct naaice_mr_dynamic_hdr) +
                        (i + 1) * sizeof(struct naaice_mr_advertisement)));
        }
        
        clock_gettime(CLOCK_MONOTONIC_RAW, &mr_exchange_done_time);
        printf("TIME MR Exchange: %.9fs\n", timediff(init_done_time, mr_exchange_done_time));

        printf("MRs set up, doing RMDA_WRITE. Preparing Data...\n");
        for (int i = 0; i < (comm_ctx->no_local_mrs); i++) {
          uint32_t *array = (uint32_t *)comm_ctx->mr_local_data[i].addr;
          memset(array, INT_DATA_SUBSTITUTE, comm_ctx->mr_local_data[i].ibv->length);
          // for (uint64_t j = 0; j < comm_ctx->mr_local_data[i].ibv->length / 4; j++) {
          //   // array[j] = htobe32((uint32_t)(j*2%UINT32_MAX));
          //   // printf("%d ",be32toh(array[j]));
          //   array[j] = htobe32((uint32_t)INT_DATA_SUBSTITUTE);
          // }
          // printf("\n");
        }
        printf("Sending data. Array(s) filled with %d's, expecting back the "
               "same incremented by one\n",
               INT_DATA_SUBSTITUTE);

        clock_gettime(CLOCK_MONOTONIC_RAW, &data_exchange_done_time[0]);

        int ret = naaice_write(comm_ctx, 0);
        if (ret) {
          comm_ctx->state = FINISHED;
        }
        return ret;
      } 
      else if (msg->type == MSG_MR_ERR) {
        struct naaice_mr_error *err =
            (struct naaice_mr_error *)(comm_ctx->mr_local_message->addr +
                                       sizeof(struct naaice_mr_hdr));
        fprintf(stderr,
                "Remote node encountered error in message exchange: %x\n", err->code);
        return -1;
      }
    }
    return 0;
  } else if (wc->opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
    printf("RDMA WRITE WITH IMM RECEIVED\n");
    printf("Receive consumed\n");
    if (ntohl(wc->imm_data)){
      fprintf(stderr,
              "Immediate value non-zero. Error: %d."
              "Disconnecting.\n",
              ntohl(wc->imm_data));
      return ntohl(wc->imm_data);
    }
      comm_ctx->repititions_completed++;
    printf("Finished repitition nr. %d\n", comm_ctx->repititions_completed);

    clock_gettime(CLOCK_MONOTONIC_RAW, &data_exchange_done_time[comm_ctx->repititions_completed]);
    printf("TIME repitition nr. %d: %.9fs\n", comm_ctx->repititions_completed,
            timediff(data_exchange_done_time[comm_ctx->repititions_completed-1], data_exchange_done_time[comm_ctx->repititions_completed]));

    if (comm_ctx->repititions_completed == NUMBER_OF_REPITITIONS) {
        printf("Checking received data...\n");

        // uint32_t errorcount = 0;
        for (int i = 0; i < (comm_ctx->no_local_mrs); i++) {
            uint32_t *array = (uint32_t *)comm_ctx->mr_local_data[i].addr;
            if (!memvcmp(array, INT_DATA_SUBSTITUTE, comm_ctx->mr_local_data[i].ibv->length)) {
                printf("ERROR: Memory Regions don't match!\n");
            }
            // for (uint64_t j = 0; j < comm_ctx->mr_local_data[i].ibv->length / 4; j++) {
            //     // printf("%u ",be32toh(array[j]));
            //     if (be32toh(array[j]) != (uint32_t)INT_DATA_SUBSTITUTE) {
            //         printf("Error found at i=%d, j=%lu, value=%d.\n", i, j, array[j]);
            //         errorcount++;
            //     }
            // }
            // printf("\n");
        }
        printf("Check done.\n");

        comm_ctx->state = FINISHED;
        printf("All repititions Finished. Completed. Disconnecting\n");
        return 1;
    }

    printf("Sending data.\n");
    comm_ctx->rdma_writes_done = 0;
    // usleep(1000*1000); // 1 sec sleep
    int ret = naaice_write(comm_ctx, 0);
    if (ret){
      comm_ctx->state = FINISHED;
    }
    return ret;
  } else if (wc->opcode == IBV_WC_SEND) {
    // printf("send completed successfully.\n");
    printf("sent out all information for every memory region. Waiting on "
           "advertisement of peer memory regions\n");
    comm_ctx->state = MR_EXCHANGE;
    return 0;
  } else if (wc->opcode == IBV_WC_RDMA_WRITE) {
    if (comm_ctx->state == FINISHED) {
      printf("Finished. Disconnecting");
      return -1;
    } /*printf("RDMA write completed successfully.");
 printf("opcode %d", wc->opcode);
 printf(" wr id: %ld ", wc->wr_id);*/
    comm_ctx->rdma_writes_done++;
    // printf("RDMA Writes done: %d\n", comm_ctx->rdma_writes_done);
    // if (comm_ctx->events_acked ==
    //     comm_ctx->events_to_ack) {
    if (comm_ctx->rdma_writes_done ==
        comm_ctx->no_local_mrs) {
      comm_ctx->state = WAIT_DATA;
      naaice_post_recv(comm_ctx);
      printf("\nAll RDMA writes completed successfully. Waiting for results\n");

      // for (int i = 0; i < (comm_ctx->no_local_mrs); i++) {
      //     memset(comm_ctx->mr_local_data[i].addr, 0, comm_ctx->mr_local_data[i].ibv->length);
      // }
      // printf("All data zeroed.\n");
    }
    // printf("Writing more\n");
    return 0;
  }
  fprintf(
      stderr,
      "work completion opcode (wc opcode): %d, not handled. Disconnecting.\n",
      wc->opcode);
  return -1;
}
int main(int argc, char *argv[]) {
  if (argc != 4) {
    fprintf(stderr, "Wrong number of arguments. use: ./naaice_client local-ip "
                    "remote-ip transfer-length\n");
    exit(EXIT_FAILURE);
  };

  clock_gettime(CLOCK_MONOTONIC_RAW, &programm_start_time);

  struct rdma_event_channel *rdma_ev_channel;
  struct rdma_cm_id *rdma_comm_id;
  struct addrinfo *rem_addr, *loc_addr;
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
  snprintf(conn_port, 6, "%d", CONNECTION_PORT);
  if (getaddrinfo(argv[1], conn_port, NULL, &loc_addr)) {
    perror("Failed to get address info for local address. Exiting.");
    exit(EXIT_FAILURE);
  }
  if (getaddrinfo(argv[2], conn_port, NULL, &rem_addr)) {
    perror("Failed to get address info for remote address. Exiting.");
    exit(EXIT_FAILURE);
  }
  if (rdma_resolve_addr(rdma_comm_id, loc_addr->ai_addr, rem_addr->ai_addr,
                        TIMEOUT_IN_MS) == -1) {
    perror("Failed to resolve addresses.");
    exit(EXIT_FAILURE);
  }
  freeaddrinfo(loc_addr);
  freeaddrinfo(rem_addr);
  if (argv[3]) {
  }
  char *p;
  errno = 0;
  transfer_length = strtoull(argv[3], &p, 10);
  if (transfer_length > (255) * MAX_TRANSFER_LENGTH) {
    fprintf(stderr, "Requested Transfer Size exceeds the maximum amount of "
                    "memory regions. Exiting\n");
    exit(EXIT_FAILURE);
  }
  if ((errno == ERANGE && (transfer_length == ULLONG_MAX)) ||
      (errno != 0 && transfer_length == 0)) {
    perror("Conversion of user-input for transfer length failed.");
    exit(EXIT_FAILURE);
  }
  while (rdma_get_cm_event(rdma_ev_channel, &ev) == 0) {
    struct rdma_cm_event ev_cp;
    memcpy(&ev_cp, ev, sizeof(*ev));
    rdma_ack_cm_event(ev);
    if (naaice_on_event(&ev_cp)) {
      break;
    }
  }
  if (rdma_destroy_id(rdma_comm_id) == -1) {
    perror("Failed to destroy RDMA communication id.");
    exit(EXIT_FAILURE);
  }
  // printf("Destroyed rdma_comm_id successfully\n");
  rdma_destroy_event_channel(rdma_ev_channel);
  printf("All cleaned up. Exiting\n");
  exit(EXIT_SUCCESS);
}
