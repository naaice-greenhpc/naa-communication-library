#include "naaice_swnaa.h"

#include <bits/pthreadtypes.h>
#include <errno.h>
#include <infiniband/verbs.h>
#include <pthread.h>
#include <rdma/rdma_cma.h>
#include <stdint.h>
#include <sys/types.h>
#include <ulog.h>

#include "config.h"
#include "naaice.h"

/* Helper Functions **********************************************************/

// Implemented in naaice.c.
const char *get_ibv_wc_opcode_str(enum ibv_wc_opcode opcode);
const char *get_state_str(naaice_communication_state state);

// Global ulog lock: the SWNAA is multi-threaded and ulog isn't thread-safe by
// default.
static pthread_mutex_t g_ulog_lock = PTHREAD_MUTEX_INITIALIZER;

// ulog lock/unlock callback backed by the global lock above.
static ulog_status naaice_swnaa_ulog_lock_fn(bool lock, void *lock_arg) {
  if (lock_arg == NULL) {
    return ULOG_STATUS_INVALID_ARGUMENT;
  }

  pthread_mutex_t *mtx = (pthread_mutex_t *)lock_arg;
  int rc = lock ? pthread_mutex_lock(mtx) : pthread_mutex_unlock(mtx);
  return (rc == 0) ? ULOG_STATUS_OK : ULOG_STATUS_ERROR;
}

/* Function Implementations **************************************************/

int naaice_swnaa_init_master(struct context **ctx, uint16_t local_cm_port,
                             rpc_function_t *rpc_func) {
  ulog_trace("In naaice_swnaa_init_master\n");
  int log_status = ulog_lock_set_fn(naaice_swnaa_ulog_lock_fn, &g_ulog_lock);
  // Accept success or logging being disabled.
  if ((log_status != ULOG_STATUS_OK) && (log_status != ULOG_STATUS_DISABLED)) {
    ulog_error("Failed to configure ulog thread lock.\n");
    return -1;
  }

  // Initialize master context.
  *ctx = (struct context *)calloc(1, sizeof(struct context));
  if (*ctx == NULL) {
    ulog_error("Memory allocation for the server_context failed");
    return -1;
  }

  pthread_mutex_init(&(*ctx)->lock, NULL);

  (*ctx)->rpc_func = rpc_func;

  (*ctx)->con_mng = (struct connection_management *)calloc(1, sizeof(struct connection_management));
  if ((*ctx)->con_mng == NULL) {
    ulog_error("Memory allocation for the connection management failed");
    return -1;
  }

  // Initialize the free-connection array and its top index.
  (*ctx)->con_mng->top = MAX_CONNECTIONS;
  for (int i = 0; i < MAX_CONNECTIONS; i++) {
    (*ctx)->con_mng->connections[i] = i;
    (*ctx)->worker[i] = NULL;
  }

  (*ctx)->master =
      (struct naaice_communication_context *)calloc(1, sizeof(struct naaice_communication_context));
  if ((*ctx)->master == NULL) {
    ulog_error("Memory allocation for the master communication context failed");
    return -1;
  }

  (*ctx)->master->state = NAAICE_INIT;
  (*ctx)->total_connections_lifetime = 0;

  // Make an event channel, checking for allocation success.
  ulog_debug("Making event channel.\n");
  (*ctx)->master->ev_channel = rdma_create_event_channel();
  if (!(*ctx)->master->ev_channel) {
    ulog_error("Failed to create the master RDMA event channel.\n");
    return -1;
  }

  // Make a communication ID, checking for allocation success.
  ulog_debug("Making communication ID.\n");
  struct rdma_cm_id *rdma_comm_id;
  if (rdma_create_id((*ctx)->master->ev_channel, &rdma_comm_id, NULL, RDMA_PS_TCP) == -1) {
    ulog_error("Failed to create master RDMA communication id.\n");
    return -1;
  }
  (*ctx)->master->id = rdma_comm_id;

  ulog_debug("Configuring connection.\n");
  struct sockaddr loc_addr;
  memset(&loc_addr, 0, sizeof(loc_addr));
  loc_addr.sa_family = AF_INET;
  ((struct sockaddr_in *)&loc_addr)->sin_port = htons(local_cm_port);

  // Bind communication ID to local address.
  ulog_debug("Bind address.\n");
  if (rdma_bind_addr((*ctx)->master->id, &loc_addr)) {
    ulog_error("Binding communication ID to local address failed.\n");
    ulog_error("errno: %d\n", errno);
    return -1;
  }

  // Listen on the port.
  if (rdma_listen((*ctx)->master->id,
                  10)) {  // Backlog queue length 10.
    ulog_error("Listening on specified port failed.\n");
    return -1;
  }

  ulog_debug("Listening on port %d.\n", ntohs(rdma_get_src_port((*ctx)->master->id)));

  return 0;
}

int naaice_swnaa_init_worker(struct context **ctx, uint8_t worker_id) {
  (*ctx)->worker[worker_id] = calloc(1, sizeof(struct naaice_communication_context));

  (*ctx)->worker[worker_id]->connection_id = worker_id;
  (*ctx)->worker[worker_id]->state = NAAICE_INIT;
  (*ctx)->worker[worker_id]->no_local_mrs = 0;
  (*ctx)->worker[worker_id]->no_peer_mrs = 0;
  (*ctx)->worker[worker_id]->no_internal_mrs = 0;
  (*ctx)->worker[worker_id]->mr_return_idx = 0;
  (*ctx)->worker[worker_id]->rdma_writes_done = 0;
  (*ctx)->worker[worker_id]->fncode = 0;
  (*ctx)->worker[worker_id]->no_input_mrs = 0;
  (*ctx)->worker[worker_id]->no_output_mrs = 0;
  (*ctx)->worker[worker_id]->immediate = 0;
  (*ctx)->worker[worker_id]->no_rpc_calls = 0;
  (*ctx)->worker[worker_id]->timeout = DEFAULT_TIMEOUT;
  (*ctx)->worker[worker_id]->retry_count = DEFAULT_RETRY_COUNT;

  // Only the MRSP region is allocated now; parameter and internal scratch
  // regions wait until MRSP completes.
  (*ctx)->worker[worker_id]->mr_local_data = NULL;

  ulog_debug("Allocating memory region for MRSP.\n");
  (*ctx)->worker[worker_id]->mr_local_message =
      (struct naaice_mr_local *)calloc(1, sizeof(struct naaice_mr_local));
  if ((*ctx)->worker[worker_id]->mr_local_message == NULL) {
    ulog_error("Failed to allocate local memory for MRSP messages.\n");
    return -1;
  }
  (*ctx)->worker[worker_id]->mr_local_message->addr = (char *)calloc(1, MR_SIZE_MRSP);
  if ((*ctx)->worker[worker_id]->mr_local_message->addr == NULL) {
    ulog_error("Failed to allocate local memory for MRSP messages.\n");
    return -1;
  }

  return 0;
}

int naaice_swnaa_init_communication_context(struct naaice_communication_context **comm_ctx) {
  ulog_trace("In naaice_swnaa_init_communication_context\n");

  // Allocate memory for the communication context.
  ulog_debug("Allocating communication context.\n");
  *comm_ctx =
      (struct naaice_communication_context *)calloc(1, sizeof(struct naaice_communication_context));
  if (comm_ctx == NULL) {
    ulog_error("Failed to allocate memory for communication context. Exiting.");
    return -1;
  }

  // Make an event channel, checking for allocation success.
  ulog_debug("Making event channel.\n");
  (*comm_ctx)->ev_channel = rdma_create_event_channel();
  if (!(*comm_ctx)->ev_channel) {
    ulog_error("Failed to create an RDMA event channel.\n");
    return -1;
  }

  // Make a communication ID, checking for allocation success.
  ulog_debug("Making communication ID.\n");
  struct rdma_cm_id *rdma_comm_id;
  if (rdma_create_id((*comm_ctx)->ev_channel, &rdma_comm_id, NULL, RDMA_PS_TCP) == -1) {
    ulog_error("Failed to create an RDMA communication id.\n");
    return -1;
  }
  (*comm_ctx)->ibv_ctx = rdma_comm_id->verbs;

  // Initialize fields of the communication context.
  (*comm_ctx)->state = NAAICE_INIT;
  (*comm_ctx)->id = rdma_comm_id;
  (*comm_ctx)->no_local_mrs = 0;
  (*comm_ctx)->no_peer_mrs = 0;
  (*comm_ctx)->no_internal_mrs = 0;
  (*comm_ctx)->mr_return_idx = 0;
  (*comm_ctx)->rdma_writes_done = 0;
  (*comm_ctx)->fncode = 0;
  (*comm_ctx)->no_input_mrs = 0;
  (*comm_ctx)->no_output_mrs = 0;
  (*comm_ctx)->immediate = 0;
  (*comm_ctx)->no_rpc_calls = 0;
  (*comm_ctx)->timeout = DEFAULT_TIMEOUT;
  (*comm_ctx)->retry_count = DEFAULT_RETRY_COUNT;

  // Only the MRSP region is allocated now; parameter and internal scratch
  // regions wait until MRSP completes.
  (*comm_ctx)->mr_local_data = NULL;

  ulog_debug("Allocating memory region for MRSP.\n");
  (*comm_ctx)->mr_local_message =
      (struct naaice_mr_local *)calloc(1, sizeof(struct naaice_mr_local));
  if ((*comm_ctx)->mr_local_message == NULL) {
    ulog_error("Failed to allocate local memory for MRSP messages.\n");
    return -1;
  }
  (*comm_ctx)->mr_local_message->addr = (char *)calloc(1, MR_SIZE_MRSP);
  if ((*comm_ctx)->mr_local_message->addr == NULL) {
    ulog_error("Failed to allocate local memory for MRSP messages.\n");
    return -1;
  }

  return 0;
}

int naaice_swnaa_setup_connection(struct context *ctx) {
  ulog_trace("In naaice_swnaa_setup_connection\n");

  // Loop handling events and updating the completion flag until finished.
  while (ctx->master->state < NAAICE_CONNECTED) {
    naaice_swnaa_poll_and_handle_connection_event(ctx);
  }

  return 0;
}

int naaice_swnaa_poll_and_handle_connection_event(struct context *ctx) {
  ulog_trace("In naaice_poll_and_handle_connection_event\n");

  struct rdma_cm_event ev;
  struct rdma_cm_event ev_cp;
  uintptr_t worker_id;
  struct naaice_communication_context *comm_ctx;
  static char *reject_reason = "Server overlad";

  if (!naaice_poll_connection_event(ctx->master, &ev, &ev_cp)) {
    switch (ev_cp.event) {
      case RDMA_CM_EVENT_CONNECT_REQUEST:
        // Reject if there is no capacity for a new connection.
        if (ctx->con_mng->top <= 0) {
          ulog_error("No capacity for a new connection");
          if (rdma_reject(ev_cp.id, reject_reason, strlen(reject_reason) + 1) == 0) {
            ulog_error("Rejected connection due to server overload");
          } else {
            ulog_error("Error on connection rejection");
          }

          return -1;
        }
        // Take a free connection slot and assign the new rdma_id to its worker.
        pthread_mutex_lock(&ctx->lock);
        worker_id = ctx->con_mng->connections[--ctx->con_mng->top];
        pthread_mutex_unlock(&ctx->lock);
        if (naaice_swnaa_init_worker(&ctx, worker_id)) {
          ulog_error("Failed to initialize worker for new connection");
          pthread_mutex_lock(&ctx->lock);
          ctx->con_mng->connections[ctx->con_mng->top++] = worker_id;
          pthread_mutex_unlock(&ctx->lock);
          return -1;
        }
        ctx->worker[worker_id]->id = ev_cp.id;
        ev_cp.id->context = (void *)(uintptr_t)worker_id;

        if (naaice_swnaa_handle_connection_requests(ctx, &ev_cp)) {
          ulog_error("Failed to handle connection request");
          naaice_swnaa_disconnect_and_cleanup(ctx->worker[worker_id]);
          pthread_mutex_lock(&ctx->lock);
          ctx->con_mng->connections[ctx->con_mng->top++] = worker_id;
          ctx->worker[worker_id] = NULL;
          pthread_mutex_unlock(&ctx->lock);
          return -1;
        }
        break;
      case RDMA_CM_EVENT_ESTABLISHED:
        worker_id = (uintptr_t)ev_cp.id->context;
        ctx->total_connections_lifetime++;
        comm_ctx = ctx->worker[worker_id];
        if (naaice_swnaa_handle_connection_established(comm_ctx, &ev_cp)) {
          ulog_error("Failed to handle connection establishment");
          pthread_mutex_lock(&ctx->lock);
          ctx->con_mng->connections[ctx->con_mng->top++] = comm_ctx->connection_id;
          ctx->worker[comm_ctx->connection_id] = NULL;
          pthread_mutex_unlock(&ctx->lock);
          naaice_swnaa_disconnect_and_cleanup(comm_ctx);
          return -1;
        }

        // Connection established: start the worker thread for it.
        struct worker_args *wargs = calloc(1, sizeof(struct worker_args));
        if (wargs == NULL) {
          ulog_error("Failed to allocate memory for worker args");
          return -1;
        }
        wargs->ctx = ctx;
        wargs->worker_id = comm_ctx->connection_id;
        pthread_create(&ctx->worker_threads[comm_ctx->connection_id], NULL, worker_procedure,
                       wargs);
        break;
      case RDMA_CM_EVENT_CONNECT_ERROR:
        ulog_debug("Error: RDMA_CM_EVENT_CONNECT_ERROR");
        break;
      case RDMA_CM_EVENT_DISCONNECTED:
        ulog_debug("Error: RDMA_CM_EVENT_DISCONNECTED");
        break;
      case RDMA_CM_EVENT_DEVICE_REMOVAL:
        worker_id = (uintptr_t)ev_cp.id->context;
        struct naaice_communication_context *check_ctx = NULL;

        pthread_mutex_lock(&ctx->lock);
        if (worker_id < MAX_CONNECTIONS && ctx->worker[worker_id] != NULL &&
            ctx->worker[worker_id]->id == ev_cp.id) {
          check_ctx = ctx->worker[worker_id];
        }
        pthread_mutex_unlock(&ctx->lock);

        // Guard the unlikely case of a second error event for a connection
        // already destroyed by the first.
        if (check_ctx == NULL) {
          ulog_error(
              "Received an error event for a connection that was already "
              "destroyed.\n");
          return 0;
        }

        if (naaice_swnaa_handle_error(ctx->worker[worker_id], &ev_cp)) {
          return -1;
        }
        break;
        // As the server we never trigger address/route resolution events, so
        // they are not handled.

      default:
        if (naaice_handle_other(ctx->master, &ev_cp)) {
          return -1;
        }
        break;
    }
  }

  // Success whether an event was handled or none was received.
  return 0;
}

void *worker_procedure(void *arg) {
  // Detach so the thread cleans up after itself when finished.
  pthread_detach(pthread_self());

  ulog_debug("in worker_procedure\n");
  struct worker_args *wargs = (struct worker_args *)arg;
  struct context *ctx = wargs->ctx;
  uint8_t worker_id = wargs->worker_id;
  struct naaice_communication_context *comm_ctx = ctx->worker[worker_id];

  free(wargs);

  ulog_info("Worker %hhu started\n", worker_id);

  naaice_swnaa_do_mrsp(comm_ctx);

  while (comm_ctx->state >= NAAICE_MRSP_DONE) {
    // Receive data transfer from host.
    ulog_info("-- Receiving Data Transfer --\n");
    if (naaice_swnaa_receive_data_transfer(comm_ctx)) {
      ulog_error("Failed in receiving data transfer");
    }
    if (comm_ctx->state < NAAICE_MRSP_DONE || comm_ctx->state == NAAICE_FINISHED) {
      break;
    }

    // Now that all data has arrived, perform the RPC.
    ulog_info("-- Doing RPC --\n");
    ulog_debug("Function Code: %d\n", comm_ctx->fncode);

    uint8_t errorcode = ctx->rpc_func(comm_ctx->fncode, comm_ctx);

    // Finally, write back the results to the host.
    if (naaice_swnaa_do_data_transfer(comm_ctx, errorcode)) {
      ulog_error("Failed to do data transfer\n");
      break;
    }
  }

  naaice_swnaa_disconnect_and_cleanup(comm_ctx);
  pthread_mutex_lock(&ctx->lock);
  ctx->con_mng->connections[ctx->con_mng->top++] = worker_id;
  ctx->worker[worker_id] = NULL;
  pthread_mutex_unlock(&ctx->lock);

  ulog_info("Worker %hhu finished, freed connection slot\n", worker_id);

  return 0;
}

int naaice_swnaa_match_event_worker(struct context *ctx, struct rdma_cm_event *ev,
                                    uint8_t *worker_id) {
  for (int i = 0; i < MAX_CONNECTIONS; i++) {
    if (ctx->worker[i] != NULL && ctx->worker[i]->id != NULL && ev->id == ctx->worker[i]->id) {
      *worker_id = (uint8_t)i;
      return 0;
    }
  }

  ulog_error("Could not match event to any worker\n");
  return -1;
}

int naaice_swnaa_handle_connection_requests(struct context *ctx, struct rdma_cm_event *ev) {
  ulog_trace("In naaice_handle_connection_requests\n");

  if (ev->event == RDMA_CM_EVENT_CONNECT_REQUEST) {
    struct rdma_conn_param cm_params;
    memset(&cm_params, 0, sizeof(cm_params));
    cm_params.retry_count = 7;
    cm_params.initiator_depth = 1;
    cm_params.responder_resources = 1;
    cm_params.rnr_retry_count = 6;  // 7 would be indefinite.

    uintptr_t worker_id = (uintptr_t)ev->id->context;
    struct naaice_communication_context *comm_ctx = ctx->worker[worker_id];
    ulog_debug("connection id %d", comm_ctx->connection_id);
    if (naaice_init_rdma_resources(comm_ctx)) {
      ulog_error("Failed in allocating RDMA resources\n");
    }

    // Register the memory region used for MRSP on the server side.
    comm_ctx->mr_local_message->ibv =
        ibv_reg_mr(comm_ctx->pd, comm_ctx->mr_local_message->addr, MR_SIZE_MRSP,
                   (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));
    if (comm_ctx->mr_local_message->ibv == NULL) {
      ulog_error("Failed to register memory for memory region setup protocol.\n");
      return -1;
    }

    if (naaice_swnaa_post_recv_mrsp(comm_ctx)) {
      long privdata = 0;
      if (rdma_reject(ctx->master->id, (void *)privdata, sizeof(privdata))) {
        ulog_error("Rejecting RDMA connection due to error failed. Exiting\n");
      }
    }
    uint8_t ack_timeout = DEFAULT_ACK_TIMEOUT;
    if (rdma_set_option(comm_ctx->id, RDMA_OPTION_ID, RDMA_OPTION_ID_ACK_TIMEOUT, &ack_timeout,
                        sizeof(ack_timeout))) {
      ulog_error("Failed to set the ack timeout.\n");
    }

    if (rdma_accept(comm_ctx->id, &cm_params)) {
      ulog_error("RDMA connection failed, in rdma_accept.\n");
      return -1;
    }
  }

  return 0;
}

int naaice_swnaa_handle_connection_established(struct naaice_communication_context *comm_ctx,
                                               struct rdma_cm_event *ev) {
  return naaice_handle_connection_established(comm_ctx, ev);
}

int naaice_swnaa_init_mrsp(struct naaice_communication_context *comm_ctx) {
  ulog_trace("In naaice_swnaa_init_mrsp\n");

  // Wait for memory region announcement and request from the host.
  naaice_swnaa_post_recv_mrsp(comm_ctx);

  return 0;
}

int naaice_swnaa_handle_error(struct naaice_communication_context *comm_ctx,
                              struct rdma_cm_event *ev) {
  ulog_trace("In naaice_swnaa_handle_error\n");

  // Returns -1 and logs for the recognized error events below, 0 otherwise.

  if (ev->event == RDMA_CM_EVENT_CONNECT_ERROR) {
    comm_ctx->state = NAAICE_ERROR;
    ulog_error("Error during connection establishment.\n");
    return -1;
  } else if (ev->event == RDMA_CM_EVENT_DEVICE_REMOVAL) {
    comm_ctx->state = NAAICE_ERROR;
    ulog_error("RDMA device was removed.\n");
    return -1;
  } else if (ev->event == RDMA_CM_EVENT_DISCONNECTED) {
    comm_ctx->state = NAAICE_FINISHED;
    // TODO(#3): is a disconnect here an error? We don't expect one at this point.
    return 0;
  }

  return 0;
}

int naaice_swnaa_do_mrsp(struct naaice_communication_context *comm_ctx) {
  ulog_trace("In naaice_swnaa_do_mrsp\n");

  // Update state.
  comm_ctx->state = NAAICE_MRSP_RECEIVING;

  // Initialize the MRSP.
  if (naaice_swnaa_init_mrsp(comm_ctx)) {
    return -1;
  }

  // Poll the completion queue and handle work completions until the MRSP is
  // complete.
  time_t start, end;
  time(&start);
  while (comm_ctx->state < NAAICE_MRSP_DONE) {
    time(&end);
    // Possible race where state is still MRSP_DONE but a wc for the recv
    // data-with-imm has already arrived. Not reliably reproducible.
    /* example output:
    In naaice_swnaa_handle_work_completion
    state: MRSP_DONE, opcode: IBV_WC_RECV_RDMA_WITH_IMM
    Work completion opcode (wc opcode): 129, not handled for state:  12.
    Error while handling work completion.
    */
    if (naaice_swnaa_poll_cq_nonblocking(comm_ctx)) {
      return -1;
    }
    if (difftime(end, start) > comm_ctx->timeout) {
      ulog_warn("Timeout while receiving MRSP from client (timeout %f).\n", comm_ctx->timeout);
      return -1;
    }
  }

  return 0;
}

int naaice_swnaa_do_data_transfer(struct naaice_communication_context *comm_ctx,
                                  uint8_t errorcode) {
  ulog_trace("In naaice_swnaa_do_data_transfer\n");

  // Update state.
  comm_ctx->state = NAAICE_DATA_SENDING;
  naaice_swnaa_write_data(comm_ctx, errorcode);
  time_t start, end;
  time(&start);

  while (comm_ctx->state == NAAICE_DATA_SENDING) {
    time(&end);
    if (naaice_swnaa_poll_cq_nonblocking(comm_ctx)) {
      return -1;
    }
    if (difftime(end, start) > comm_ctx->timeout) {
      ulog_warn("Timeout while sending data to client.\n");
      return -1;
    }
  }
  return 0;
}

int naaice_swnaa_receive_data_transfer(struct naaice_communication_context *comm_ctx) {
  ulog_trace("In naaice_swnaa_receive_data_transfer\n");

  // Increment number of RPC calls.
  comm_ctx->no_rpc_calls++;

  comm_ctx->state = NAAICE_DATA_RECEIVING;

  // Post a receive for the data.
  naaice_swnaa_post_recv_data(comm_ctx);

  // Poll the completion queue and handle work completions until the data
  // transfer to the NAA is complete.
  time_t start, end;
  time(&start);

  while (comm_ctx->state == NAAICE_DATA_RECEIVING) {
    time(&end);
    if (naaice_swnaa_poll_cq_nonblocking(comm_ctx)) {
      return -1;
    }
    if (difftime(end, start) > comm_ctx->timeout) {
      ulog_warn("Timeout while receiving data from client.\n");
      return -1;
    }
  }

  return 0;
}

int naaice_swnaa_post_recv_mrsp(struct naaice_communication_context *comm_ctx) {
  ulog_trace("In naaice_swnaa_post_recv_mrsp\n");

  // Reuses the host-side logic.
  return naaice_post_recv_mrsp(comm_ctx);
}

int naaice_swnaa_handle_work_completion(struct ibv_wc *wc,
                                        struct naaice_communication_context *comm_ctx) {
  ulog_trace("In naaice_swnaa_handle_work_completion\n");

  ulog_debug("state: %s, opcode: %s\n", get_state_str(comm_ctx->state),
             get_ibv_wc_opcode_str(wc->opcode));

  // If the work completion status is not success, return with error.
  if (wc->status != IBV_WC_SUCCESS) {
    ulog_error("Status is not IBV_WC_SUCCESS. Status %d for operation %d.\n", wc->status,
               wc->opcode);
    return -1;
  }

  // If we are still waiting for the MRSP packet...
  if (comm_ctx->state == NAAICE_MRSP_RECEIVING) {
    // If we're recieving an MRSP packet...
    if (wc->opcode == IBV_WC_RECV) {
      // The message should have been written to the memory region we allocated
      // for MRSP messages. Grab the header from there.
      struct naaice_mr_hdr *msg = (struct naaice_mr_hdr *)comm_ctx->mr_local_message->addr;

      // If the message was an announce + request...
      if (msg->type == MSG_MR_AAR) {
        // Work completion details, for debugging.
        /*
        ulog_debug("Work Completion (MRSP):\n");
        ulog_debug("wr_id: %ld\n", wc->wr_id);
        ulog_debug("status: %d\n", wc->status);
        ulog_debug("opcode: %d\n", wc->opcode);
        ulog_debug("vendor_err: %08X\n", wc->vendor_err);
        ulog_debug("byte_len: %d\n", wc->byte_len);
        ulog_debug("imm_data: %d\n", wc->imm_data);
        ulog_debug("qp_num: %d\n", wc->qp_num);
        ulog_debug("src_qp: %d\n", wc->src_qp);
        ulog_debug("wc_flags: %x\n", wc->wc_flags);
        ulog_debug("slid: %d\n", wc->slid);
        ulog_debug("sl: %d\n", wc->sl);
        ulog_debug("dlid_path_bits: %d\n", wc->dlid_path_bits);
        */

        if (naaice_swnaa_handle_mr_announce_and_request(comm_ctx)) {
          // If an error occurs, send an error message to the host.
          ulog_debug("Error while handling MR announce and request.\n");
          naaice_swnaa_send_message(comm_ctx, MSG_MR_ERR, 1);
          return -1;
        }

        ulog_debug("Send message backt to client: MRSP succesul");
        // Otherwise, send an announcement back.
        naaice_swnaa_send_message(comm_ctx, MSG_MR_A, 0);

        return 0;
      }

      // Return with error if a remote error has occurred.
      else if (msg->type == MSG_MR_ERR) {
#ifndef ULOG_BUILD_DISABLED
        struct naaice_mr_error *err = (struct naaice_mr_error *)(comm_ctx->mr_local_message->addr +
                                                                 sizeof(struct naaice_mr_error));
        ulog_error("Remote node encountered error in message exchange: %d\n", err->code);
#endif
        return -1;
      }

      // Otherwise, some weird message type. Return with error.
      else {
        ulog_error("Unhandled MRSP packet type received: %d\n", msg->type);
        return -1;
      }
    }
  }

  // If we are sending the MRSP response to the host...
  else if (comm_ctx->state == NAAICE_MRSP_SENDING) {
    // If we have sent the packet...
    if (wc->opcode == IBV_WC_SEND) {
      // NAA-side of MRSP done.
      // Update state.
      comm_ctx->state = NAAICE_MRSP_DONE;
      return 0;
    }
  }

  // If we are waiting for data from the host...
  else if (comm_ctx->state == NAAICE_DATA_RECEIVING) {
    // If we received data without an immediate...
    if (wc->opcode == IBV_WC_RECV) {
      // Shouldn't happen; a write does not trigger a recv. Possibly an
      // error case.
      return 0;
    }
    // If we have received a write with immediate (i.e. the last parameter)...
    else if (wc->opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
      // Check if the immediate value is zero, indicating an error.
      if (!ntohl(wc->imm_data)) {
        ulog_error("Received write with immediate value zero.\n");
        return -1;
      }

      // Otherwise, we can set the function code based on the 7 least
      // significant bits of the immediate value
      comm_ctx->fncode = (uint8_t)ntohl(wc->imm_data) & 0x7F;
      comm_ctx->immediate = (ntohl(wc->imm_data) & 0xFFFFFF00) >> 8;

      // Work completion details, for debugging.
      /*
      ulog_debug("Work Completion (Data):\n");
      ulog_debug("wr_id: %ld\n", wc->wr_id);
      ulog_debug("status: %d\n", wc->status);
      ulog_debug("opcode: %d\n", wc->opcode);
      ulog_debug("vendor_err: %08X\n", wc->vendor_err);
      ulog_debug("byte_len: %d\n", wc->byte_len);
      ulog_debug("imm_data: %d\n", wc->imm_data);
      ulog_debug("qp_num: %d\n", wc->qp_num);
      ulog_debug("src_qp: %d\n", wc->src_qp);
      ulog_debug("wc_flags: %x\n", wc->wc_flags);
      ulog_debug("slid: %d\n", wc->slid);
      ulog_debug("sl: %d\n", wc->sl);
      ulog_debug("dlid_path_bits: %d\n", wc->dlid_path_bits);
      */

      // Update state.
      comm_ctx->state = NAAICE_CALCULATING;

      // Now we are ready to perform the NAA procedure.
      return 0;
    }
  } else if (comm_ctx->state == NAAICE_DATA_SENDING) {
    // Sending data back. If we've written some data...
    if (wc->opcode == IBV_WC_RDMA_WRITE) {
      // Count the completed write.
      comm_ctx->rdma_writes_done++;
      ulog_debug("rdma writes done: %d\n", comm_ctx->rdma_writes_done);

      // Once all writes are done, wait for the next request.
      if (comm_ctx->rdma_writes_done == comm_ctx->no_output_mrs) {
        // Skip straight to DATA_RECEIVING; the CALCULATING state is used only by
        // the software NAA.
        comm_ctx->state = NAAICE_DATA_RECEIVING;
        comm_ctx->rdma_writes_done = 0;
      }

      return 0;
    }
  }

  // Opcode not handled for the current state: return with error.
  ulog_error("Work completion opcode (wc opcode): %d, not handled for state:  %d.\n", wc->opcode,
             comm_ctx->state);
  return -1;
}

int naaice_swnaa_poll_cq_nonblocking(struct naaice_communication_context *comm_ctx) {
  ulog_trace("In naaice_swnaa_poll_cq_nonblocking\n");

  struct ibv_cq *ev_cq;
  void *ev_ctx;

  // Ensure completion channel is in non-blocking mode.
  int fd_flags = fcntl(comm_ctx->comp_channel->fd, F_GETFL);
  if (fcntl(comm_ctx->comp_channel->fd, F_SETFL, fd_flags | O_NONBLOCK) < 0) {
    ulog_error(
        "Failed to change file descriptor of completion event "
        "channel.\n");
    return -1;
  }

  struct pollfd my_pollfd;
  int ms_timeout = 0;
  // Poll the completion channel, returning with flag unchanged if nothing
  // is received.
  my_pollfd.fd = comm_ctx->comp_channel->fd;
  my_pollfd.events = POLLIN;
  my_pollfd.revents = 0;

  // Nonblocking: if poll times out, just return.
  int poll_result = poll(&my_pollfd, 1, ms_timeout);
  if (poll_result < 0) {
    // Likely an error; no events would return 0.
    ulog_error("Error occured when polling completion channel.\n");
    return -1;
  } else if (poll_result == 0) {
    // No events received.
    return 0;
  }

  // If something is received, get the completion event.
  if (ibv_get_cq_event(comm_ctx->comp_channel, &ev_cq, &ev_ctx)) {
    ulog_error("Failed to get completion queue event.\n");
    return -1;
  }

  // Ack the completion event.
  ibv_ack_cq_events(ev_cq, 1);

  // While there are work completions in the completion queue, handle them.
  struct ibv_wc wc;
  naaice_communication_state state = comm_ctx->state;
  int n_wcs = ibv_poll_cq(comm_ctx->cq, 1, &wc);
  ulog_debug("number of polled elements: %d\n", n_wcs);

  // If ibv_poll_cq returns an error, return.
  if (n_wcs < 0) {
    ulog_error("ibv_poll_cq() failed.\n");
    return -1;
  }

  while (n_wcs) {
    // Handle the work completion.
    if (naaice_swnaa_handle_work_completion(&wc, comm_ctx)) {
      ulog_error("Error while handling work completion.\n");
      return -1;
    }

    // TODO(#4): centralize state changes in one place.
    if (state != comm_ctx->state && comm_ctx->state > NAAICE_MRSP_SENDING) {
      ulog_debug("State has changed\n");
      // State changed: move forward before polling the next event.
      break;
    }

    // Find any remaining work completions in the queue.
    n_wcs = ibv_poll_cq(comm_ctx->cq, 1, &wc);
    if (n_wcs < 0) {
      ulog_error("ibv_poll_cq() failed.\n");
      return -1;
    }
  }

  // Request completion channel notifications for the next event.
  if (ibv_req_notify_cq(comm_ctx->cq, 0)) {
    ulog_error(

        "Failed to request completion channel notifications on completion "
        "queue.\n");
    return -1;
  }

  return 0;
}

int naaice_swnaa_handle_mr_announce_and_request(struct naaice_communication_context *comm_ctx) {
  // The request packet's mrflags and fpgaaddress fields tell an FPGA-based NAA
  // how to allocate memory regions; the software NAA ignores them.

  // TODO(#8): mrflags may need handling, sourced from config / RMS.

  ulog_trace("In naaice_swnaa_handle_mr_announce_and_request\n");

  // First read the header.
  struct naaice_mr_dynamic_hdr *dyn =
      (struct naaice_mr_dynamic_hdr *)(comm_ctx->mr_local_message->addr +
                                       sizeof(struct naaice_mr_hdr));

  // Get the number of advertised memory regions.
  uint8_t n_advertised_mrs = dyn->count;

  // First pass: count "normal" memory regions (input/output parameters) and
  // requested internal regions.

  // Current read position in the packet.
  struct naaice_mr_advertisement_request *curr;

  for (int i = 0; i < n_advertised_mrs; i++) {
    // Point to next position in the packet.
    curr = (struct naaice_mr_advertisement_request
                *)(comm_ctx->mr_local_message->addr +
                   (sizeof(struct naaice_mr_hdr) + sizeof(struct naaice_mr_dynamic_hdr) +
                    (i) * sizeof(struct naaice_mr_advertisement_request)));

    // Get memory region info. Includes MR flags and requested address.
    uint64_t mr_info = ntohll(curr->mr_info);
    uint8_t *mr_info_bytearray = (uint8_t *)&mr_info;
    uint8_t mr_flags = mr_info_bytearray[7];

    // Internal region by its flags, otherwise a "normal" region.
    if (mr_flags & MRFLAG_INTERNAL) {
      comm_ctx->no_internal_mrs++;
    } else {
      comm_ctx->no_local_mrs++;
    }
  }

  // Storage for local memory region info, excluding internal regions.
  comm_ctx->mr_local_data =
      (struct naaice_mr_local *)calloc(comm_ctx->no_local_mrs, sizeof(struct naaice_mr_local));
  if (comm_ctx->mr_local_data == NULL) {
    ulog_error("Failed to allocate memory for local memory region structures.\n");
    return -1;
  }

  // Symmetric regions, so peer count equals the number of "normal" ones.
  comm_ctx->no_peer_mrs = comm_ctx->no_local_mrs;

  // Storage for peer memory region info.
  comm_ctx->mr_peer_data =
      (struct naaice_mr_peer *)calloc(comm_ctx->no_peer_mrs, sizeof(struct naaice_mr_peer));
  if (comm_ctx->mr_peer_data == NULL) {
    ulog_error(
        "Failed to allocate memory for remote memory region "
        "structures.\n");
    return -1;
  }

  // Storage for internal memory region info.
  comm_ctx->mr_internal = (struct naaice_mr_internal *)calloc(comm_ctx->no_internal_mrs,
                                                              sizeof(struct naaice_mr_internal));

  // Second pass: per region, fill the relevant structs and allocate its buffer.
  // Normal regions are also registered with ibv.

  // Separate running counts for internal and normal regions.
  uint8_t local_count = 0, internal_count = 0;
  for (int i = 0; i < n_advertised_mrs; i++) {
    // Point to next position in the packet.
    curr = (struct naaice_mr_advertisement_request
                *)(comm_ctx->mr_local_message->addr +
                   (sizeof(struct naaice_mr_hdr) + sizeof(struct naaice_mr_dynamic_hdr) +
                    (i) * sizeof(struct naaice_mr_advertisement_request)));

    // Get memory region info. Includes MR flags and requested address.
    uint64_t mr_info = ntohll(curr->mr_info);
    uint8_t *mr_info_bytearray = (uint8_t *)&mr_info;
    uint8_t mr_flags = mr_info_bytearray[7];

    // If this is an internal memory region...
    if (mr_flags & MRFLAG_INTERNAL) {
      // Requested FPGA MR address: unused by the software NAA, read only to
      // confirm it is being set properly.
      __attribute__((unused)) uint8_t fpgaaddress[8];
      for (int j = 0; j < 7; j++) {
        fpgaaddress[j] = mr_info_bytearray[j];
      }
      fpgaaddress[7] = 0;

      // Allocate memory for the region.
      // TODO(#8): set this address based on the fpgaaddr field.
      comm_ctx->mr_internal[internal_count].addr = (uint64_t)calloc(1, ntohl(curr->size));
      if (comm_ctx->mr_internal[internal_count].addr == 0 /* NULL */) {
        ulog_error("Failed to allocate memory for internal memory region buffer.\n");
        return -1;
      }

      // Set the size of the memory region.
      comm_ctx->mr_internal[internal_count].size = ntohl(curr->size);

      ulog_debug("Internal MR %d: Addr: %lX, Size: %d, Requested Addr: %lX\n", internal_count + 1,
                 (uintptr_t)comm_ctx->mr_internal[internal_count].addr,
                 (int)comm_ctx->mr_internal[internal_count].size, (uint64_t)*fpgaaddress);

      // Increment count.
      internal_count++;
    }

    // Otherwise, if this is a "normal" memory region...
    else {
      // Set peer memory region fields.
      comm_ctx->mr_peer_data[local_count].addr = ntohll(curr->addr);
      comm_ctx->mr_peer_data[local_count].rkey = ntohl(curr->rkey);
      comm_ctx->mr_peer_data[local_count].size = ntohl(curr->size);

      // Requested FPGA MR address: unused by the software NAA, read only to
      // confirm it is being set properly.
      __attribute__((unused)) uint8_t fpgaaddress[8];
      for (int j = 0; j < 7; j++) {
        fpgaaddress[j] = mr_info_bytearray[j];
      }
      fpgaaddress[7] = 0;

      // Allocate memory for the region.
      comm_ctx->mr_local_data[local_count].addr =
          (char *)calloc(1, comm_ctx->mr_peer_data[local_count].size);
      if (comm_ctx->mr_local_data[local_count].addr == NULL) {
        ulog_error("Failed to allocate memory for local memory region buffer.\n");
        return -1;
      }

      // Register the memory region.
      comm_ctx->mr_local_data[local_count].ibv =
          ibv_reg_mr(comm_ctx->pd, comm_ctx->mr_local_data[local_count].addr,
                     comm_ctx->mr_peer_data[local_count].size,
                     (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));
      if (comm_ctx->mr_local_data[local_count].ibv == NULL) {
        ulog_error("Failed to register memory for local memory region.\n");
        return -1;
      }

      // Set the size of the memory region.
      comm_ctx->mr_local_data[local_count].size = comm_ctx->mr_local_data[local_count].ibv->length;

      // to_write flags come from the MR info byte.
      comm_ctx->mr_peer_data[local_count].to_write = (bool)(mr_flags & MRFLAG_INPUT);
      comm_ctx->no_input_mrs += (uint8_t)comm_ctx->mr_peer_data[local_count].to_write;
      comm_ctx->mr_local_data[local_count].to_write = (bool)(mr_flags & MRFLAG_OUTPUT);
      comm_ctx->no_output_mrs += (uint8_t)comm_ctx->mr_local_data[local_count].to_write;

      // single_send flag from the info byte.
      comm_ctx->mr_peer_data[local_count].single_send = (bool)(mr_flags & MRFLAG_SINGLESEND);
      comm_ctx->mr_local_data[local_count].single_send = (bool)(mr_flags & MRFLAG_SINGLESEND);

      ulog_debug(
          "Local MR %d: Addr: %lX, Size: %lu, Requested Addr: %lX, is "
          "output: %d\n",
          local_count + 1, (uintptr_t)comm_ctx->mr_local_data[local_count].addr,
          comm_ctx->mr_local_data[local_count].ibv->length, (uint64_t)*fpgaaddress,
          comm_ctx->mr_local_data[local_count].to_write);

      ulog_debug("Peer MR %d: Addr: %lX, Size: %lu, rkey: %u, is input: %d\n", local_count + 1,
                 (uintptr_t)comm_ctx->mr_peer_data[local_count].addr,
                 comm_ctx->mr_peer_data[local_count].size, comm_ctx->mr_peer_data[local_count].rkey,
                 comm_ctx->mr_peer_data[local_count].to_write);

      // Increment count.
      local_count++;
    }
  }

  return 0;
}

int naaice_swnaa_send_message(struct naaice_communication_context *comm_ctx,
                              enum message_id message_type, uint8_t errorcode) {
  ulog_trace("In naaice_swnaa_send_message\n");

  // Update state.
  comm_ctx->state = NAAICE_MRSP_SENDING;

  // Like naaice_send_message, but the server may not send request messages to
  // the host.

  // Messages are built in the dedicated region allocated in
  // naaice_init_communication_context, starting with a header.
  struct naaice_mr_hdr *msg = (struct naaice_mr_hdr *)comm_ctx->mr_local_message->addr;

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
      curr = (struct naaice_mr_advertisement *)(msg + sizeof(struct naaice_mr_hdr) +
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

  // If we're sending an error message...
  if (message_type == MSG_MR_ERR) {
    // Insert error packet. No dynamic header for this message type.
    struct naaice_mr_error *err = (struct naaice_mr_error *)(msg + sizeof(struct naaice_mr_hdr));

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
  wr.wr_id = msg->type;  // (uintptr_t)comm_ctx;
  wr.opcode = IBV_WR_SEND;
  wr.sg_list = &sge;
  wr.num_sge = 1;
  wr.send_flags = IBV_SEND_SOLICITED;

  // Send the packet.
  int post_result = ibv_post_send(comm_ctx->qp, &wr, &bad_wr);
  if (post_result) {
    ulog_error("Posting send for MRSP failed with error %d.\n", post_result);
    return -1;
  }

  return 0;
}

int naaice_swnaa_post_recv_data(struct naaice_communication_context *comm_ctx) {
  ulog_trace("In naaice_swnaa_post_recv_data\n");

  // A single empty recv request suffices; input/output region info comes
  // from the host's MRSP announcement MR flags.

  // Construct a single, simple recv request.
  struct ibv_recv_wr wr;
  struct ibv_recv_wr *bad_wr = NULL;
  struct ibv_sge sge;

  memset(&wr, 0, sizeof(struct ibv_recv_wr));
  wr.wr_id = 0;
  wr.sg_list = &sge;
  wr.num_sge = 1;
  wr.next = NULL;

  sge.addr = 0;
  sge.length = 0;
  sge.lkey = comm_ctx->mr_local_data[0].ibv->lkey;

  ulog_debug("recv addr: %p, length: %d, lkey %d\n", (void *)sge.addr, sge.length, sge.lkey);

  // Post the receive.
  int post_result = ibv_post_recv(comm_ctx->qp, &wr, &bad_wr);
  if (post_result) {
    ulog_error("Posting recieve for data failed with error %d.\n", post_result);
    return post_result;
  }

  return 0;
}

int naaice_swnaa_write_data(struct naaice_communication_context *comm_ctx, uint8_t errorcode) {
  ulog_trace("In naaice_swnaa_write_data\n");

  // Update state.
  comm_ctx->state = NAAICE_DATA_SENDING;

  // If there are no memory regions to write back, return an error.
  if (comm_ctx->no_local_mrs < 1) {
    ulog_error("No local memory regions to write back.\n");
    return -1;
  }

  // On a computation error, send the first region's first byte (a 0-byte
  // transfer is impossible) with a nonzero immediate marking the error.
  if (errorcode) {
    ulog_error("Error occured during NAA routine computation: %d.\n", errorcode);

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
    // TODO(#8): support multiple return regions (error path).
    wr.wr.rdma.remote_addr = comm_ctx->mr_peer_data[comm_ctx->mr_return_idx].addr;
    wr.wr.rdma.rkey = comm_ctx->mr_peer_data[comm_ctx->mr_return_idx].rkey;

    // Post the send.
    int post_result = ibv_post_send(comm_ctx->qp, &wr, &bad_wr);
    if (post_result) {
      ulog_error(
          "Posting send for data write "
          "(while sending error message) failed with error %d.\n",
          post_result);
      return post_result;
    }
  }

  // Otherwise, write back every region whose local to_write flag marks it an
  // output parameter (set via naaice_swnaa_set_output_mr).
  else {
    // Get number of output regions to be sent.
    uint8_t n_output_mrs = 0;
    for (unsigned int i = 0; i < comm_ctx->no_local_mrs; i++) {
      if (comm_ctx->mr_local_data[i].to_write) {
        n_output_mrs++;
      }
    }

    // We will have one write request (and one scatter/gather elements) for
    // each memory region to be written.
    struct ibv_send_wr wr[n_output_mrs], *bad_wr = NULL;
    struct ibv_sge sge[n_output_mrs];

    // Construct write requests and scatter/gather elements for all memory
    // regions to be sent.
    uint8_t mr_idx = 0;
    for (int i = 0; (i < comm_ctx->no_local_mrs) && (mr_idx < n_output_mrs); i++) {
      if (comm_ctx->mr_local_data[i].to_write) {
        ulog_debug("output mr %d (local index %d):\n", mr_idx, i);
        memset(&wr[mr_idx], 0, sizeof(wr[mr_idx]));

        wr[mr_idx].wr_id = mr_idx + 1;
        wr[mr_idx].sg_list = &sge[mr_idx];
        wr[mr_idx].num_sge = 1;

        wr[mr_idx].wr.rdma.remote_addr = comm_ctx->mr_peer_data[i].addr;
        wr[mr_idx].wr.rdma.rkey = comm_ctx->mr_peer_data[i].rkey;

        // Last region uses a write-with-immediate (immediate 0); the rest are
        // normal writes.
        if (mr_idx == n_output_mrs - 1) {
          wr[mr_idx].imm_data = htonl(0);
          wr[mr_idx].opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
          wr[mr_idx].send_flags = IBV_SEND_SOLICITED;
          wr[mr_idx].next = NULL;
        } else {
          wr[mr_idx].opcode = IBV_WR_RDMA_WRITE;
          wr[mr_idx].next = &wr[mr_idx + 1];
        }

        sge[mr_idx].addr = (uintptr_t)comm_ctx->mr_local_data[i].addr;
        sge[mr_idx].length = comm_ctx->mr_local_data[i].ibv->length;
        sge[mr_idx].lkey = comm_ctx->mr_local_data[i].ibv->lkey;

        mr_idx++;
      }
    }

    // Post the send.
    int post_result = ibv_post_send(comm_ctx->qp, &wr[0], &bad_wr);
    if (post_result) {
      ulog_error(
          "Posting send for data write "
          "failed with error %d.\n",
          post_result);
      return post_result;
    }
  }

  // No state update, to support multiple RPC invocations.

  return 0;
}

int naaice_swnaa_disconnect_and_cleanup(struct naaice_communication_context *comm_ctx) {
  ulog_trace("In naaice_swnaa_disconnect_and_cleanup\n");

  // Differs slightly from the host side: all local memory regions can be freed,
  // as they are not in user memory space.

  // Disconnect is done by the client exclusively.

  // Deregister memory regions.
  int err = 0;
  err = ibv_dereg_mr(comm_ctx->mr_local_message->ibv);
  if (err) {
    ulog_error(
        "Deregestering local message memory region failed with "
        "error %d.\n",
        err);
    return -1;
  }
  for (int i = 0; i < comm_ctx->no_local_mrs; i++) {
    err = ibv_dereg_mr(comm_ctx->mr_local_data[i].ibv);
    if (err) {
      ulog_error(
          "Deregestering local data memory region failed with "
          "error %d.\n",
          err);
      return -1;
    }
    free((void *)(comm_ctx->mr_local_data[i].addr));
  }

  if (comm_ctx->mr_peer_data != NULL) {
    free(comm_ctx->mr_peer_data);
    comm_ctx->mr_peer_data = NULL;
  }

  if (comm_ctx->mr_internal != NULL) {
    free(comm_ctx->mr_internal);
    comm_ctx->mr_internal = NULL;
  }

  free((void *)(comm_ctx->mr_local_message->addr));
  free(comm_ctx->mr_local_message);
  free(comm_ctx->mr_local_data);
  free(comm_ctx->id);
  free(comm_ctx->ev_channel);

  // Destroy queue pair.
  err = ibv_destroy_qp(comm_ctx->qp);
  if (err) {
    ulog_error("Destroying queue pair failed with error %d.\n", err);
    return -1;
  }

  // Destroy completion queue.
  err = ibv_destroy_cq(comm_ctx->cq);
  if (err) {
    ulog_error(
        "Destroying completion queue failed with "
        "error %d.\n",
        err);
    return -1;
  }

  // Destroy completion channel.
  err = ibv_destroy_comp_channel(comm_ctx->comp_channel);
  if (err) {
    ulog_error(
        "Destroying completion channel failed with "
        "error %d.\n",
        err);
    return -1;
  }

  // Destroy protection domain.
  err = ibv_dealloc_pd(comm_ctx->pd);
  if (err) {
    ulog_error(
        "Destroying protection domain failed with "
        "error %d.\n",
        err);
    return -1;
  }

  free(comm_ctx);

  return 0;
}
