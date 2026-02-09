/**************************************************************************
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
 * 26-01-2024
 *
 *****************************************************************************/

// Enable debug messages.
#include "naaice_swnaa.h"
#include "naaice.h"
#include <errno.h>
#include <infiniband/verbs.h>
#include <pthread.h>
#include <rdma/rdma_cma.h>
#include <stdint.h>
#include <swnaa_kernels.h>
#include <sys/types.h>
#include <ulog.h>

/* Helper Functions **********************************************************/

// Implemented in naaice.c.
const char *get_ibv_wc_opcode_str(enum ibv_wc_opcode opcode);
const char *get_state_str(enum naaice_communication_state state);

int naaice_swnaa_consume_connection(struct connection_management *con_mng,
                                    uint8_t *worker_id);

/* Function Implementations **************************************************/

int naaice_swnaa_init_master(struct context **ctx, uint16_t port) {

  log_trace("In naaice_swnaa_init_master\n");

  // initialize master context
  *ctx = (struct context *)malloc(sizeof(struct context));
  if (*ctx == NULL) {
    log_error("Malloc of server_context failed");
    return -1;
  }

  pthread_mutex_init(&(*ctx)->lock, NULL);

  (*ctx)->con_mng = (struct connection_management *)malloc(
      sizeof(struct connection_management));
  // initalize array and position of free connections
  (*ctx)->con_mng->top = MAX_CONNECTIONS;
  for (int i = 0; i < MAX_CONNECTIONS; i++) {
    (*ctx)->con_mng->connections[i] = i;
    (*ctx)->worker[i] = NULL;
  }

  (*ctx)->master = (struct naaice_communication_context *)malloc(
      sizeof(struct naaice_communication_context));

  (*ctx)->master->state = NAAICE_INIT;

  // Make an event channel, checking for allocation success.
  log_debug("Making event channel.\n");
  (*ctx)->master->ev_channel = rdma_create_event_channel();
  if (!(*ctx)->master->ev_channel) {
    log_error("Failed to create the master RDMA event channel.\n");
    return -1;
  }

  // Make a communication ID, checking for allocation success.
  log_debug("Making communication ID.\n");
  struct rdma_cm_id *rdma_comm_id;
  if (rdma_create_id((*ctx)->master->ev_channel, &rdma_comm_id, NULL,
                     RDMA_PS_TCP) == -1) {
    log_error("Failed to create master RDMA communication id.\n");
    return -1;
  }
  (*ctx)->master->id = rdma_comm_id;

  log_debug("Configuring connection.\n");
  struct sockaddr loc_addr;
  memset(&loc_addr, 0, sizeof(loc_addr));
  loc_addr.sa_family = AF_INET;
  ((struct sockaddr_in *)&loc_addr)->sin_port = htons(port);

  // Bind communication ID to local address.
  log_debug("Bind address.\n");
  if (rdma_bind_addr((*ctx)->master->id, &loc_addr)) {
    log_error("Binding communication ID to local address failed.\n");
    log_error("errno: %d\n", errno);
    return -1;
  }

  // Listen on the port.
  if (rdma_listen((*ctx)->master->id,
                  10)) { // Backlog queue length 10.
    log_error("Listening on specified port failed.\n");
    return -1;
  }

  int port_num = ntohs(rdma_get_src_port((*ctx)->master->id));
  log_debug("Listening on port %d.\n", port_num);

  return 0;
}

int naaice_swnaa_init_worker(struct context **ctx, uint8_t worker_id) {

  (*ctx)->worker[worker_id] =
      malloc(sizeof(struct naaice_communication_context));

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

  // The memory region used for MRSP is allocated here, but the ones for the
  // parameters and the internal memory regions used for NAA scratch
  // computation are not until after MRSP is complete.
  (*ctx)->worker[worker_id]->mr_local_data = NULL;

  log_debug("Allocating memory region for MRSP.\n");
  (*ctx)->worker[worker_id]->mr_local_message =
      (struct naaice_mr_local *)calloc(1, sizeof(struct naaice_mr_local));
  if ((*ctx)->worker[worker_id]->mr_local_message == NULL) {
    log_error("Failed to allocate local memory for MRSP messages.\n");
    return -1;
  }
  (*ctx)->worker[worker_id]->mr_local_message->addr =
      (char *)calloc(1, MR_SIZE_MRSP);
  if ((*ctx)->worker[worker_id]->mr_local_message->addr == NULL) {
    log_error("Failed to allocate local memory for MRSP messages.\n");
    return -1;
  }

  return 0;
}

int naaice_swnaa_init_communication_context(
    struct naaice_communication_context **comm_ctx, uint16_t port) {

  log_trace("In naaice_swnaa_init_communication_context\n");

  // Allocate memory for the communication context.
  log_debug("Allocating communication context.\n");
  *comm_ctx = (struct naaice_communication_context *)malloc(
      sizeof(struct naaice_communication_context));
  if (comm_ctx == NULL) {
    log_error("Failed to allocate memory for communication context. Exiting.");
    return -1;
  }

  // Make an event channel, checking for allocation success.
  log_debug("Making event channel.\n");
  (*comm_ctx)->ev_channel = rdma_create_event_channel();
  if (!(*comm_ctx)->ev_channel) {
    log_error("Failed to create an RDMA event channel.\n");
    return -1;
  }

  // Make a communication ID, checking for allocation success.
  log_debug("Making communication ID.\n");
  struct rdma_cm_id *rdma_comm_id;
  if (rdma_create_id((*comm_ctx)->ev_channel, &rdma_comm_id, NULL,
                     RDMA_PS_TCP) == -1) {
    log_error("Failed to create an RDMA communication id.\n");
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

  // The memory region used for MRSP is allocated here, but the ones for the
  // parameters and the internal memory regions used for NAA scratch
  // computation are not until after MRSP is complete.
  (*comm_ctx)->mr_local_data = NULL;

  log_debug("Allocating memory region for MRSP.\n");
  (*comm_ctx)->mr_local_message =
      (struct naaice_mr_local *)calloc(1, sizeof(struct naaice_mr_local));
  if ((*comm_ctx)->mr_local_message == NULL) {
    log_error("Failed to allocate local memory for MRSP messages.\n");
    return -1;
  }
  (*comm_ctx)->mr_local_message->addr = (char *)calloc(1, MR_SIZE_MRSP);
  if ((*comm_ctx)->mr_local_message->addr == NULL) {
    log_error("Failed to allocate local memory for MRSP messages.\n");
    return -1;
  }

  // Configure connection.
  // TODO: make port flexible?
  // Probably with RMS support, when we ask for the NAA ip, we will also get
  // port num.
  log_debug("Configuring connection.\n");
  struct sockaddr loc_addr;
  memset(&loc_addr, 0, sizeof(loc_addr));
  loc_addr.sa_family = AF_INET;
  ((struct sockaddr_in *)&loc_addr)->sin_port = htons(port);

  // Bind communication ID to local address.
  if (rdma_bind_addr(rdma_comm_id, &loc_addr)) {
    log_error("Binding communication ID to local address failed.\n");
    log_error("errno: %d\n", errno);
    return -1;
  }

  // Listen on the port.
  if (rdma_listen(rdma_comm_id, 10)) { // Backlog queue length 10.
    log_error("Listening on specified port failed.\n");
    return -1;
  }

  int port_num = ntohs(rdma_get_src_port(rdma_comm_id));
  log_debug("Listening on port %d.\n", port_num);

  return 0;
}

int naaice_swnaa_setup_connection(
    struct naaice_communication_context *comm_ctx) {

  log_trace("In naaice_swnaa_setup_connection\n");

  // Loop handling events and updating the completion flag until finished.
  while (comm_ctx->state < NAAICE_CONNECTED) {

    naaice_swnaa_poll_and_handle_connection_event(comm_ctx);
  }

  return 0;
}

int naaice_swnaa_setup_connection_multi(struct context *ctx) {

  log_debug("In naaice_swnaa_setup_connection\n");

  // Loop handling events and updating the completion flag until finished.
  while (ctx->master->state < NAAICE_CONNECTED) {

    naaice_swnaa_poll_and_handle_connection_event_multi(ctx);
  }

  return 0;
}

int naaice_swnaa_poll_and_handle_connection_event(
    struct naaice_communication_context *comm_ctx) {

  // log_trace("In naaice_poll_and_handle_connection_event\n");

  // If we've received an event...
  struct rdma_cm_event ev;
  struct rdma_cm_event ev_cp;

  if (!naaice_poll_connection_event(comm_ctx, &ev, &ev_cp)) {
    comm_ctx->id = ev_cp.id;
    log_warn("connection event id: %d", ev_cp.id);
    switch (ev_cp.event) {
    case RDMA_CM_EVENT_CONNECT_REQUEST:
      if (naaice_swnaa_handle_connection_requests(comm_ctx, &ev_cp)) {
        return -1;
      }
      break;
    case RDMA_CM_EVENT_ESTABLISHED:
      if (naaice_swnaa_handle_connection_established(comm_ctx, &ev_cp)) {
        return -1;
      }
      break;
    case RDMA_CM_EVENT_CONNECT_ERROR:
    case RDMA_CM_EVENT_DISCONNECTED:
    case RDMA_CM_EVENT_DEVICE_REMOVAL:
      if (naaice_swnaa_handle_error(comm_ctx, &ev_cp)) {
        return -1;
      }
      break;
      // Since we're the server we don't handle addr resolution etc (we never
      // make calls that would create such an event)

    default:
      if (naaice_handle_other(comm_ctx, &ev_cp)) {
        return -1;
      }
      break;
    }
  }

  // If we sucessfully handled an event (or haven't received one), success.
  return 0;
}

int naaice_swnaa_poll_and_handle_connection_event_multi(struct context *ctx) {

  log_debug("In naaice_poll_and_handle_connection_event\n");

  // If we've received an event...
  struct rdma_cm_event ev;
  struct rdma_cm_event ev_cp;
  uint8_t worker_id = '\0';
  struct naaice_communication_context *comm_ctx;

  if (!naaice_poll_connection_event(ctx->master, &ev, &ev_cp)) {

    switch (ev_cp.event) {
    case RDMA_CM_EVENT_CONNECT_REQUEST:
      // check if there is the capacity for a new connection
      if (ctx->con_mng->top <= 0) {
        log_error("No capacity for a new connection");
        return -1;
      }
      // in the case of a connection request assign the new rdma_id to the
      // worker
      pthread_mutex_lock(&ctx->lock);
      int worker_idx = ctx->con_mng->connections[--ctx->con_mng->top];
      if (naaice_swnaa_init_worker(&ctx, worker_idx)) {
        log_error("Failed to initialize worker for new connection");
        pthread_mutex_unlock(&ctx->lock);
        return -1;
      }
      ctx->worker[worker_idx]->id = ev_cp.id;
      ev_cp.id->context = ctx->worker[worker_idx];
      pthread_mutex_unlock(&ctx->lock);

      if (naaice_swnaa_handle_connection_requests_multi(ctx, &ev_cp)) {
        return -1;
      }
      break;
    case RDMA_CM_EVENT_ESTABLISHED:
      comm_ctx = ev_cp.id->context;
      log_debug("Connection established event for worker %hhu\n",
                comm_ctx->connection_id);
      if (naaice_swnaa_handle_connection_established(comm_ctx, &ev_cp)) {
        return -1;
      }

      struct worker_args *wargs = malloc(sizeof(struct worker_args));
      wargs->ctx = ctx;
      wargs->worker_id = comm_ctx->connection_id;
      pthread_create(&ctx->worker_threads[worker_id], NULL, worker_procedure,
                     wargs);

      log_debug("before break");
      break;
    case RDMA_CM_EVENT_CONNECT_ERROR:
      log_debug("Error: RDMA_CM_EVENT_CONNECT_ERROR");
    case RDMA_CM_EVENT_DISCONNECTED:
      log_debug("Error: RDMA_CM_EVENT_DISCONNECTED");
    case RDMA_CM_EVENT_DEVICE_REMOVAL:
      comm_ctx = ev_cp.id->context;
      uint8_t worker_id = comm_ctx->connection_id;
      printf("worker_id (event error): %d\n", worker_id);
      if (naaice_swnaa_handle_error(ctx->worker[worker_id], &ev_cp)) {
        return -1;
      }

      //       comm_ctx = ev_cp.id->context;
      // int worker_id = comm_ctx->connection_id;
      // printf("worker_id (event error): %d\n", worker_id);
      // if (pthread_cancel(ctx->worker_threads[worker_id])) {
      //   log_error("Error on canceling worker tread");
      // } else {
      //   log_debug("Worker thread canceled successfully");
      //   pthread_mutex_lock(&ctx->lock);
      //   naaice_swnaa_disconnect_and_cleanup_multi(ctx->worker[worker_id]);
      //   ctx->con_mng->connections[ctx->con_mng->top++] = worker_id;
      //   pthread_mutex_unlock(&ctx->lock);
      // }
      // if (naaice_swnaa_handle_error(ctx->worker[worker_id], &ev_cp)) {
      //   return -1;
      // }

      break;
      // Since we're the server we don't handle addr resolution etc (we never
      // make calls that would create such an event)

    default:
      if (naaice_handle_other(ctx->master, &ev_cp)) {
        return -1;
      }
      break;
    }
  }

  // If we sucessfully handled an event (or haven't received one), success.
  return 0;
}

void *worker_procedure(void *arg) {

  log_debug("in worker_procedure\n");
  struct worker_args *wargs = (struct worker_args *)arg;
  struct context *ctx = wargs->ctx;
  uint8_t worker_id = wargs->worker_id;
  struct naaice_communication_context *comm_ctx = ctx->worker[worker_id];

  free(wargs);

  log_info("Worker %hhu started\n", worker_id);

  naaice_swnaa_do_mrsp(comm_ctx);

  while (comm_ctx->state >= NAAICE_MRSP_DONE) {

    // Receive data transfer from host.
    log_info("-- Receiving Data Transfer --\n");
    if (naaice_swnaa_receive_data_transfer_multi(comm_ctx)) {
      log_error("Failed in receiving data transfer");
    }
    if (comm_ctx->state < NAAICE_MRSP_DONE ||
        comm_ctx->state == NAAICE_FINISHED) {
      break;
    }

    // Now that all data has arrived, perform the RPC.
    log_info("-- Doing RPC --\n");
    log_debug("Function Code: %d\n", comm_ctx->fncode);

    if (comm_ctx->fncode) {
      void (*worker_func)(struct naaice_communication_context *) = NULL;

      match_function_code(comm_ctx->fncode, &worker_func);
      if (worker_func == NULL) {
        log_error(
            "Invalid function code received, no matching worker function.\n");
        break;
      } else {
        log_debug("Run function with function code: %d", comm_ctx->fncode);
        worker_func(comm_ctx);
      }

      // for (unsigned int i = 0; i < comm_ctx->no_local_mrs; i++) {

      //   unsigned char *data = (unsigned char
      //   *)comm_ctx->mr_local_data[i].addr;

      //   for (unsigned int j = 0; j < comm_ctx->mr_local_data[i].size; j++) {
      //     data[j]++;
      //   }
      // }
    }

    sleep(3);

    uint8_t errorcode = 0;

    // Finally, write back the results to the host.
    log_info("-- Writing Back Data --\n");
    if (naaice_swnaa_do_data_transfer(comm_ctx, errorcode)) {
      log_error("Failed to do data transfer\n");
      break;
    }
  }

  pthread_mutex_lock(&ctx->lock);
  naaice_swnaa_disconnect_and_cleanup_multi(comm_ctx);
  ctx->con_mng->top++;
  pthread_mutex_unlock(&ctx->lock);

  log_info("Worker %hhu finished, freed connection slot\n", worker_id);

  return 0;
}

int naaice_swnaa_match_event_worker(struct context *ctx,
                                    struct rdma_cm_event *ev,
                                    uint8_t *worker_id) {

  for (int i = 0; i < MAX_CONNECTIONS; i++) {
    if (ctx->worker[i] != NULL && ctx->worker[i]->id != NULL &&
        ev->id == ctx->worker[i]->id) {
      *worker_id = (uint8_t)i;
      return 0;
    }
  }

  log_error("Could not match event to any worker\n");
  return -1;
}

int naaice_swnaa_handle_connection_requests(
    struct naaice_communication_context *comm_ctx, struct rdma_cm_event *ev) {

  log_trace("In naaice_handle_connection_requests\n");

  if (ev->event == RDMA_CM_EVENT_CONNECT_REQUEST) {
    struct rdma_conn_param cm_params;
    memset(&cm_params, 0, sizeof(cm_params));
    cm_params.retry_count = 7;
    cm_params.initiator_depth = 1;
    cm_params.responder_resources = 1;
    cm_params.rnr_retry_count = 6; // 7 would be indefinite

    if (naaice_init_rdma_resources(comm_ctx)) {
      log_error("Failed in allocating RDMA resources\n");
    }

    // Register the memory region used for MRSP on the server side.
    comm_ctx->mr_local_message->ibv =
        ibv_reg_mr(comm_ctx->pd, comm_ctx->mr_local_message->addr, MR_SIZE_MRSP,
                   (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));
    if (comm_ctx->mr_local_message->ibv == NULL) {
      log_error(
          "Failed to register memory for memory region setup protocol.\n");
      return -1;
    }

    if (naaice_swnaa_post_recv_mrsp(comm_ctx)) {
      long privdata = 0;
      if (rdma_reject(comm_ctx->id, (void *)privdata, sizeof(privdata))) {
        log_error("Rejecting RDMA connection due to error failed. Exiting\n");
      }
    }
    if (rdma_accept(comm_ctx->id, &cm_params)) {
      log_error("RDMA connection failed, in rdma_accept.\n");
      return -1;
    }
  }

  return 0;
}

int naaice_swnaa_handle_connection_requests_multi(struct context *ctx,
                                                  struct rdma_cm_event *ev) {

  log_trace("In naaice_handle_connection_requests multi\n");

  if (ev->event == RDMA_CM_EVENT_CONNECT_REQUEST) {
    struct rdma_conn_param cm_params;
    memset(&cm_params, 0, sizeof(cm_params));
    cm_params.retry_count = 7;
    cm_params.initiator_depth = 1;
    cm_params.responder_resources = 1;
    cm_params.rnr_retry_count = 6; // 7 would be indefinite

    struct naaice_communication_context *comm_ctx = ev->id->context;
    log_debug("connection id %d", comm_ctx->connection_id);
    if (naaice_init_rdma_resources(comm_ctx)) {
      log_error("Failed in allocating RDMA resources\n");
    }

    // Register the memory region used for MRSP on the server side.
    comm_ctx->mr_local_message->ibv =
        ibv_reg_mr(comm_ctx->pd, comm_ctx->mr_local_message->addr, MR_SIZE_MRSP,
                   (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));
    if (comm_ctx->mr_local_message->ibv == NULL) {
      log_error(
          "Failed to register memory for memory region setup protocol.\n");
      return -1;
    }

    if (naaice_swnaa_post_recv_mrsp(comm_ctx)) {
      long privdata = 0;
      if (rdma_reject(ctx->master->id, (void *)privdata, sizeof(privdata))) {
        log_error("Rejecting RDMA connection due to error failed. Exiting\n");
      }
    }
    if (rdma_accept(comm_ctx->id, &cm_params)) {
      log_error("RDMA connection failed, in rdma_accept.\n");
      return -1;
    }
  }

  return 0;
}

int naaice_swnaa_handle_connection_established(
    struct naaice_communication_context *comm_ctx, struct rdma_cm_event *ev) {

  return naaice_handle_connection_established(comm_ctx, ev);
}

int naaice_swnaa_init_mrsp(struct naaice_communication_context *comm_ctx) {

  log_trace("In naaice_swnaa_init_mrsp\n");

  // Wait for memory region announcement and request from the host.
  naaice_swnaa_post_recv_mrsp(comm_ctx);

  return 0;
}

int naaice_swnaa_handle_error(struct naaice_communication_context *comm_ctx,
                              struct rdma_cm_event *ev) {

  log_debug("In naaice_handle_error\n");

  // Returns -1 and prints an error message if the event is one of
  // the following identified error types.
  // Otherwise returns 0.

  // set error state to be able to exit upstream loop in
  // naaice_setup_connection
  // comm_ctx->state = ERROR;

  if (ev->event == RDMA_CM_EVENT_CONNECT_ERROR) {
    comm_ctx->state = NAAICE_ERROR;
    log_error("Error during connection establishment.\n");
    return -1;
  } else if (ev->event == RDMA_CM_EVENT_DEVICE_REMOVAL) {
    comm_ctx->state = NAAICE_ERROR;
    log_error("RDMA device was removed.\n");
    return -1;
  } else if (ev->event == RDMA_CM_EVENT_DISCONNECTED) {
    comm_ctx->state = NAAICE_FINISHED;
    log_warn("event id: %d", ev->id);
    // FM What to do here? is this an error state in this case? Check what needs
    //  to be cleaned up in which state...
    //  We're not expecting disconnect at this point, so we should exit.
    log_warn("RDMA disconnected by client request.\n");
    // Keep current state for error free cleanup (depending on the state, we
    // allocated different structures)
    // comm_ctx->state = DISCONNECTED;
    // Handle disconnect.
    // naaice_swnaa_disconnect_and_cleanup(comm_ctx);
    return 0;
  }

  return 0;
}

int naaice_swnaa_handle_error_multi(
    struct naaice_communication_context *comm_ctx, struct rdma_cm_event *ev) {

  log_debug("In naaice_handle_error\n");

  // Returns -1 and prints an error message if the event is one of
  // the following identified error types.
  // Otherwise returns 0.

  // set error state to be able to exit upstream loop in
  // naaice_setup_connection
  // comm_ctx->state = ERROR;

  if (ev->event == RDMA_CM_EVENT_CONNECT_ERROR) {
    comm_ctx->state = NAAICE_ERROR;
    log_error("Error during connection establishment.\n");
    return -1;
  } else if (ev->event == RDMA_CM_EVENT_DEVICE_REMOVAL) {
    comm_ctx->state = NAAICE_ERROR;
    log_error("RDMA device was removed.\n");
    return -1;
  } else if (ev->event == RDMA_CM_EVENT_DISCONNECTED) {
    comm_ctx->state = NAAICE_FINISHED;
    log_warn("event id: %d", ev->id);
    // FM What to do here? is this an error state in this case? Check what needs
    //  to be cleaned up in which state...
    //  We're not expecting disconnect at this point, so we should exit.
    log_warn("RDMA disconnected by client request.\n");
    // Keep current state for error free cleanup (depending on the state, we
    // allocated different structures)
    // comm_ctx->state = DISCONNECTED;
    // Handle disconnect.
    // naaice_swnaa_disconnect_and_cleanup(comm_ctx);
    return 0;
  }

  return 0;
}

int naaice_swnaa_do_mrsp(struct naaice_communication_context *comm_ctx) {

  log_trace("In naaice_swnaa_do_mrsp\n");

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
    // FM: I think I encountered a race condition where we are still in
    // MRSP_DONE
    //  but have already received a wc for the recv data with imm....I just
    //  can't reproduce it reliably
    /*** example output:
    In naaice_swnaa_handle_work_completion
    state: MRSP_DONE, opcode: IBV_WC_RECV_RDMA_WITH_IMM
    Work completion opcode (wc opcode): 129, not handled for state:  12.
    Error while handling work completion.
    */
    if (naaice_swnaa_poll_cq_nonblocking(comm_ctx)) {
      return -1;
    }
    if (difftime(end, start) > comm_ctx->timeout) {
      log_warn("Timeout while receiving MRSP from client (timeout %f).\n",
               comm_ctx->timeout);
      return -1;
    }
  }

  return 0;
}

int naaice_swnaa_do_data_transfer(struct naaice_communication_context *comm_ctx,
                                  uint8_t errorcode) {

  log_trace("In naaice_swnaa_do_data_transfer\n");

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
      log_warn("Timeout while sending data to client.\n");
      return -1;
    }
  }
  return 0;
}

int naaice_swnaa_receive_data_transfer(
    struct naaice_communication_context *comm_ctx) {

  log_trace("In naaice_swnaa_receive_data_transfer\n");

  // Increment number of RPC calls.
  comm_ctx->no_rpc_calls++;
  // Check if connection event is available
  int fd_flags = fcntl(comm_ctx->ev_channel->fd, F_GETFL);
  if (fcntl(comm_ctx->ev_channel->fd, F_SETFL, fd_flags | O_NONBLOCK) < 0) {
    log_error("Failed to change file descriptor of rdma event "
              "channel.\n");
    return -1;
  }

  // Else continue, there was no event.
  // Update state.
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
    if (naaice_swnaa_poll_and_handle_connection_event(comm_ctx)) {
      return -1;
    };
    if (difftime(end, start) > comm_ctx->timeout) {
      log_warn("Timeout while receiving data from client.\n");
      return -1;
    }
  }

  return 0;
}

int naaice_swnaa_receive_data_transfer_multi(
    struct naaice_communication_context *comm_ctx) {

  log_trace("In naaice_swnaa_receive_data_transfer multi\n");

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
      log_warn("Timeout while receiving data from client.\n");
      return -1;
    }
  }

  return 0;
}

int naaice_swnaa_post_recv_mrsp(struct naaice_communication_context *comm_ctx) {

  log_trace("In naaice_swnaa_post_recv_mrsp\n");

  // Can simply call same logic used on the host side here.
  return naaice_post_recv_mrsp(comm_ctx);
}

int naaice_swnaa_handle_work_completion(
    struct ibv_wc *wc, struct naaice_communication_context *comm_ctx) {

  log_trace("In naaice_swnaa_handle_work_completion\n");

  log_debug("state: %s, opcode: %s\n", get_state_str(comm_ctx->state),
            get_ibv_wc_opcode_str(wc->opcode));

  // If the work completion status is not success, return with error.
  if (wc->status != IBV_WC_SUCCESS) {
    log_error("Status is not IBV_WC_SUCCESS. Status %d for operation %d.\n",
              wc->status, wc->opcode);
    return -1;
  }

  // If we are still waiting for the MRSP packet...
  if (comm_ctx->state == NAAICE_MRSP_RECEIVING) {

    // If we're recieving an MRSP packet...
    if (wc->opcode == IBV_WC_RECV) {

      // The message should have been written to the memory region we allocated
      // for MRSP messages. Grab the header from there.
      struct naaice_mr_hdr *msg =
          (struct naaice_mr_hdr *)comm_ctx->mr_local_message->addr;

      // If the message was an announce + request...
      if (msg->type == MSG_MR_AAR) {

        // Print all information about the work completion.
        /*
        log_debug("Work Completion (MRSP):\n");
        log_debug("wr_id: %ld\n", wc->wr_id);
        log_debug("status: %d\n", wc->status);
        log_debug("opcode: %d\n", wc->opcode);
        log_debug("vendor_err: %08X\n", wc->vendor_err);
        log_debug("byte_len: %d\n", wc->byte_len);
        log_debug("imm_data: %d\n", wc->imm_data);
        log_debug("qp_num: %d\n", wc->qp_num);
        log_debug("src_qp: %d\n", wc->src_qp);
        log_debug("wc_flags: %x\n", wc->wc_flags);
        log_debug("slid: %d\n", wc->slid);
        log_debug("sl: %d\n", wc->sl);
        log_debug("dlid_path_bits: %d\n", wc->dlid_path_bits);
        */

        if (naaice_swnaa_handle_mr_announce_and_request(comm_ctx)) {

          // If an error occurs, send an error message to the host.
          log_debug("Error while handling MR announce and request.\n");
          naaice_swnaa_send_message(comm_ctx, MSG_MR_ERR, 1);
          return -1;
        }

        log_debug("Send message backt to client: MRSP succesul");
        // Otherwise send an announcement back.
        naaice_swnaa_send_message(comm_ctx, MSG_MR_A, 0);

        return 0;
      }

      // Return with error if a remote error has occured.
      else if (msg->type == MSG_MR_ERR) {
        struct naaice_mr_error *err =
            (struct naaice_mr_error *)(comm_ctx->mr_local_message->addr +
                                       sizeof(struct naaice_mr_error));
        log_error("Remote node encountered error in message exchange: %d\n",
                  err->code);
        return -1;
      }

      // Otherwise, some weird message type. Return with error.
      else {
        log_error("Unhandled MRSP packet type received: %d\n", msg->type);
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

    // If we recieved data without an immediate...
    if (wc->opcode == IBV_WC_RECV) {
      // FM: This shouldnt happen, receiving a write does not trigger a recv. we
      // should not handle this and throw an error No need to do anything.
      return 0;
    }
    // If we have received a write with immediate (i.e. the last parameter)...
    else if (wc->opcode == IBV_WC_RECV_RDMA_WITH_IMM) {

      // Check if the immediate value is zero, indicating an error.
      if (!ntohl(wc->imm_data)) {
        log_error("Received write with immediate value zero.\n");
        return -1;
      }

      // Otherwise, we can set the function code based on the 7 least
      // significant bits of the immediate value
      comm_ctx->fncode = (uint8_t)ntohl(wc->imm_data) & 0x7F;
      comm_ctx->immediate = ntohl(wc->imm_data) & 0xFFFFFF00;

      // Print all information about the work completion.
      /*
      log_debug("Work Completion (Data):\n");
      log_debug("wr_id: %ld\n", wc->wr_id);
      log_debug("status: %d\n", wc->status);
      log_debug("opcode: %d\n", wc->opcode);
      log_debug("vendor_err: %08X\n", wc->vendor_err);
      log_debug("byte_len: %d\n", wc->byte_len);
      log_debug("imm_data: %d\n", wc->imm_data);
      log_debug("qp_num: %d\n", wc->qp_num);
      log_debug("src_qp: %d\n", wc->src_qp);
      log_debug("wc_flags: %x\n", wc->wc_flags);
      log_debug("slid: %d\n", wc->slid);
      log_debug("sl: %d\n", wc->sl);
      log_debug("dlid_path_bits: %d\n", wc->dlid_path_bits);
      */

      // log_debug("transfer size: %d\n", wc->byte_len);

      // Update state.
      comm_ctx->state = NAAICE_CALCULATING;

      // Now we are ready to perform the NAA procedure.
      return 0;
    }
  } else if (comm_ctx->state == NAAICE_DATA_SENDING) {
    // We're sending back data
    // If we recieved data without an immediate...
    // If we've written some data...
    if (wc->opcode == IBV_WC_RDMA_WRITE) {
      // comm_ctx->state = DATA_RECEIVING;
      // Increment the number of completed writes.
      comm_ctx->rdma_writes_done++;
      log_debug("rdma writes done: %d\n", comm_ctx->rdma_writes_done);

      // If all writes have been completed, start waiting for the response.
      if (comm_ctx->rdma_writes_done == comm_ctx->no_output_mrs) {

        // Update state.
        // We skip straight to DATA_RECEIVING; the CALCULATING state is used
        // only by the software NAA.
        comm_ctx->state = NAAICE_DATA_RECEIVING;
        comm_ctx->rdma_writes_done = 0;
      }

      return 0;
    }
  }

  // If we've reached this point, the work completion had an opcode which is
  // not handled for the current state, so return with error.
  log_error(
      "Work completion opcode (wc opcode): %d, not handled for state:  %d.\n",
      wc->opcode, comm_ctx->state);
  return -1;
}

int naaice_swnaa_poll_cq_nonblocking(
    struct naaice_communication_context *comm_ctx) {

  log_trace("In naaice_swnaa_poll_cq_nonblocking\n");

  struct ibv_cq *ev_cq;
  void *ev_ctx;

  // Ensure completion channel is in non-blocking mode.
  int fd_flags = fcntl(comm_ctx->comp_channel->fd, F_GETFL);
  if (fcntl(comm_ctx->comp_channel->fd, F_SETFL, fd_flags | O_NONBLOCK) < 0) {
    log_error("Failed to change file descriptor of completion event "
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
    // FM: This is probably an error. If none is received, we get back 0.
    log_error("Error occured when polling completion channel.\n");
    return -1;
  } else if (poll_result == 0) {

    // We have simply not recieved any events.
    return 0;
  }

  // If something is received, get the completion event.
  if (ibv_get_cq_event(comm_ctx->comp_channel, &ev_cq, &ev_ctx)) {
    log_error("Failed to get completion queue event.\n");
    return -1;
  }

  // Ack the completion event.
  ibv_ack_cq_events(ev_cq, 1);

  // While there are work completions in the completion queue, handle them.
  struct ibv_wc wc;
  enum naaice_communication_state state = comm_ctx->state;
  int n_wcs = ibv_poll_cq(comm_ctx->cq, 1, &wc);
  log_debug("number of polled elements: %d\n", n_wcs);

  // If ibv_poll_cq returns an error, return.
  if (n_wcs < 0) {
    log_error("ibv_poll_cq() failed.\n");
    return -1;
  }

  while (n_wcs) {
    // Handle the work completion.
    if (naaice_swnaa_handle_work_completion(&wc, comm_ctx)) {
      log_error("Error while handling work completion.\n");
      return -1;
    }

    // DYL TODO: As mentioned elsewhere by you Florian, this sort of logic is
    // not ideal. I should update it so that state changes all occur in one
    // function.
    if (state != comm_ctx->state && comm_ctx->state > NAAICE_MRSP_SENDING) {
      log_debug("State has changed\n");
      // State has changed, so we should move forward before polling the next
      // event.
      break;
    }

    // Find any remaining work completions in the queue.
    n_wcs = ibv_poll_cq(comm_ctx->cq, 1, &wc);
    if (n_wcs < 0) {
      log_error("ibv_poll_cq() failed.\n");
      return -1;
    }
  }

  // Request completion channel notifications for the next event.
  if (ibv_req_notify_cq(comm_ctx->cq, 0)) {
    log_error(

        "Failed to request completion channel notifications on completion "
        "queue.\n");
    return -1;
  }

  return 0;
}

int naaice_swnaa_handle_mr_announce_and_request(
    struct naaice_communication_context *comm_ctx) {

  // The request packet includes the fields mrflags and fpgaaddress, which
  // specify how an FPGA-based NAA should handle memory region allocation.
  // However, in the software NAA, we can simply ignore these fields and
  // allocate as normal.
  // FM: In future we might need to handle mrflags maybe?
  // Dylan: Yes for sure. This info should come from the config files / memory
  // management service / RMS

  log_trace("In naaice_swnaa_handle_mr_announce_and_request\n");

  // First read the header.
  struct naaice_mr_dynamic_hdr *dyn =
      (struct naaice_mr_dynamic_hdr *)(comm_ctx->mr_local_message->addr +
                                       sizeof(struct naaice_mr_hdr));

  // Get the number of advertised memory regions.
  uint8_t n_advertised_mrs = dyn->count;

  // First, iterate through the message and count the number of "normal" memory
  // regions (i.e. those for input and output parameters) and the number of
  // requested internal memory regions.

  // Pointer to current position in packet being read.
  struct naaice_mr_advertisement_request *curr;

  for (int i = 0; i < n_advertised_mrs; i++) {

    // Point to next position in the packet.
    curr = (struct naaice_mr_advertisement_request
                *)(comm_ctx->mr_local_message->addr +
                   (sizeof(struct naaice_mr_hdr) +
                    sizeof(struct naaice_mr_dynamic_hdr) +
                    (i) * sizeof(struct naaice_mr_advertisement_request)));

    // Get memory region info. Includes MR flags and requested address.
    // log_debug("naa: mr_info: %lX\n", curr->mr_info);
    uint64_t mr_info = ntohll(curr->mr_info);
    uint8_t *mr_info_bytearray = (uint8_t *)&mr_info;
    uint8_t mr_flags = mr_info_bytearray[7];

    // If the memory region flags indicate that this is an internal memory
    // region, increment the number of those. Otherwise this is a "normal"
    // memory region.
    if (mr_flags & MRFLAG_INTERNAL) {
      comm_ctx->no_internal_mrs++;
    } else {
      comm_ctx->no_local_mrs++;
    }
  }

  // Allocate memory to hold information about local memory regions.
  // This doesn't include the internal memory regions.
  comm_ctx->mr_local_data = (struct naaice_mr_local *)calloc(
      comm_ctx->no_local_mrs, sizeof(struct naaice_mr_local));
  if (comm_ctx->mr_local_data == NULL) {
    log_error(
        "Failed to allocate memory for local memory region structures.\n");
    return -1;
  }

  // Number of peer memory regions is the number of "normal" ones advertised,
  // because we're using symmetric memory regions.
  comm_ctx->no_peer_mrs = comm_ctx->no_local_mrs;

  // Allocate memory to hold information about peer memory regions.
  comm_ctx->mr_peer_data = (struct naaice_mr_peer *)calloc(
      comm_ctx->no_peer_mrs, sizeof(struct naaice_mr_peer));
  if (comm_ctx->mr_peer_data == NULL) {
    log_error("Failed to allocate memory for remote memory region "
              "structures.\n");
    return -1;
  }

  // Allocate memory to hold information about internal memory regions.
  comm_ctx->mr_internal = (struct naaice_mr_internal *)calloc(
      comm_ctx->no_internal_mrs, sizeof(struct naaice_mr_internal));

  // Now iterate through the memory regions again.
  // For a "normal" (aka not internal) memory region, set the fields in the
  // associated peer memory region and local memory region structs, allocate
  // memory for the region locally, and register the memory region with ibv.
  // For an internal memory region, set the fields in the associated internal
  // memory region struct and allocate memory for the region locally.

  // We have to keep separate counts of internal and normal MRs as we go.
  uint8_t local_count = 0, internal_count = 0;
  for (int i = 0; i < n_advertised_mrs; i++) {

    // Point to next position in the packet.
    curr = (struct naaice_mr_advertisement_request
                *)(comm_ctx->mr_local_message->addr +
                   (sizeof(struct naaice_mr_hdr) +
                    sizeof(struct naaice_mr_dynamic_hdr) +
                    (i) * sizeof(struct naaice_mr_advertisement_request)));

    // Get memory region info. Includes MR flags and requested address.
    uint64_t mr_info = ntohll(curr->mr_info);
    uint8_t *mr_info_bytearray = (uint8_t *)&mr_info;
    uint8_t mr_flags = mr_info_bytearray[7];

    // If this is an internal memory region...
    if (mr_flags & MRFLAG_INTERNAL) {

      // Check requested FPGA MR address.
      // These are not actually needed for the software NAA; just print them
      // to be sure they are being set properly.
      uint8_t fpgaaddress[8];
      for (int j = 0; j < 7; j++) {
        fpgaaddress[j] = mr_info_bytearray[j];
      }
      fpgaaddress[7] = 0;

      // Allocate memory for the region.
      // TODO: This address needs to be set based on the fpgaaddr field.
      comm_ctx->mr_internal[internal_count].addr =
          (uint64_t)calloc(1, ntohl(curr->size));
      if (comm_ctx->mr_internal[internal_count].addr == 0 /* NULL */) {
        log_error(
            "Failed to allocate memory for internal memory region buffer.\n");
        return -1;
      }

      // Set the size of the memory region.
      comm_ctx->mr_internal[internal_count].size = ntohl(curr->size);

      log_debug("Internal MR %d: Addr: %lX, Size: %d, Requested Addr: %lX\n",
                internal_count + 1,
                (uintptr_t)comm_ctx->mr_internal[internal_count].addr,
                (int)comm_ctx->mr_internal[internal_count].size,
                (uint64_t)*fpgaaddress);

      // Increment count.
      internal_count++;
    }

    // Otherwise, if this is a "normal" memory region...
    else {

      // Set peer memory region fields.
      comm_ctx->mr_peer_data[local_count].addr = ntohll(curr->addr);
      comm_ctx->mr_peer_data[local_count].rkey = ntohl(curr->rkey);
      comm_ctx->mr_peer_data[local_count].size = ntohl(curr->size);

      // Check requested FPGA MR address.
      // These are not actually needed for the software NAA; just print them
      // to be sure they are being set properly.
      uint8_t fpgaaddress[8];
      for (int j = 0; j < 7; j++) {
        fpgaaddress[j] = mr_info_bytearray[j];
      }
      fpgaaddress[7] = 0;

      // Allocate memory for the region.
      comm_ctx->mr_local_data[local_count].addr =
          (char *)calloc(1, comm_ctx->mr_peer_data[local_count].size);
      if (comm_ctx->mr_local_data[local_count].addr == NULL) {
        log_error(
            "Failed to allocate memory for local memory region buffer.\n");
        return -1;
      }

      // Register the memory region.
      comm_ctx->mr_local_data[local_count].ibv =
          ibv_reg_mr(comm_ctx->pd, comm_ctx->mr_local_data[local_count].addr,
                     comm_ctx->mr_peer_data[local_count].size,
                     (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));
      if (comm_ctx->mr_local_data[local_count].ibv == NULL) {
        log_error("Failed to register memory for local memory region.\n");
        return -1;
      }

      // Set the size of the memory region.
      comm_ctx->mr_local_data[local_count].size =
          comm_ctx->mr_local_data[local_count].ibv->length;

      // The to_write flag for all MRs is initialized based on the MR info byte.
      comm_ctx->mr_peer_data[local_count].to_write =
          (bool)(mr_flags & MRFLAG_INPUT);
      comm_ctx->no_input_mrs +=
          (uint8_t)comm_ctx->mr_peer_data[local_count].to_write;
      comm_ctx->mr_local_data[local_count].to_write =
          (bool)(mr_flags & MRFLAG_OUTPUT);
      comm_ctx->no_output_mrs +=
          (uint8_t)comm_ctx->mr_local_data[local_count].to_write;

      // Initialize the single_send flag based on info byte.
      comm_ctx->mr_peer_data[local_count].single_send =
          (bool)(mr_flags & MRFLAG_SINGLESEND);
      comm_ctx->mr_local_data[local_count].single_send =
          (bool)(mr_flags & MRFLAG_SINGLESEND);

      log_debug("Local MR %d: Addr: %lX, Size: %lu, Requested Addr: %lX, is "
                "output: %d\n",
                local_count + 1,
                (uintptr_t)comm_ctx->mr_local_data[local_count].addr,
                comm_ctx->mr_local_data[local_count].ibv->length,
                (uint64_t)*fpgaaddress,
                comm_ctx->mr_local_data[local_count].to_write);

      log_debug("Peer MR %d: Addr: %lX, Size: %lu, rkey: %u, is input: %d\n",
                local_count + 1,
                (uintptr_t)comm_ctx->mr_peer_data[local_count].addr,
                comm_ctx->mr_peer_data[local_count].size,
                comm_ctx->mr_peer_data[local_count].rkey,
                comm_ctx->mr_peer_data[local_count].to_write);

      // Increment count.
      local_count++;
    }
  }

  return 0;
}

int naaice_swnaa_send_message(struct naaice_communication_context *comm_ctx,
                              enum message_id message_type, uint8_t errorcode) {

  log_trace("In naaice_swnaa_send_message\n");

  // Update state.
  comm_ctx->state = NAAICE_MRSP_SENDING;

  // This is the same as naaice_send_message, except that request messages are
  // not allowed to be sent from the server to host.

  // All messages start with a header.
  // We use a dedicated memory region to construct the message, allocated in
  // naaice_init_communication_context.
  struct naaice_mr_hdr *msg =
      (struct naaice_mr_hdr *)comm_ctx->mr_local_message->addr;

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
      curr = (struct naaice_mr_advertisement
                  *)(msg + sizeof(struct naaice_mr_hdr) +
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
    struct naaice_mr_error *err =
        (struct naaice_mr_error *)(msg + sizeof(struct naaice_mr_hdr));

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
  // wr.send_flags = IBV_SEND_SIGNALED;
  wr.send_flags = IBV_SEND_SOLICITED;

  // Send the packet.
  int post_result = ibv_post_send(comm_ctx->qp, &wr, &bad_wr);
  if (post_result) {
    log_error("Posting send for MRSP failed with error %d.\n", post_result);
    return -1;
  }

  return 0;
}

int naaice_swnaa_post_recv_data(struct naaice_communication_context *comm_ctx) {

  log_trace("In naaice_swnaa_post_recv_data\n");

  // DYL: Changed this back to constructing only a single, empty recv request.
  // Information about input and output regions is recieved from the host
  // during MRSP in the announcement packet MR flags.

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

  log_debug("recv addr: %p, length: %d, lkey %d\n", (void *)sge.addr,
            sge.length, sge.lkey);

  // Post the recieve.
  int post_result = ibv_post_recv(comm_ctx->qp, &wr, &bad_wr);
  if (post_result) {
    log_error("Posting recieve for data failed with error %d.\n", post_result);
    return post_result;
  }

  return 0;
}

int naaice_swnaa_write_data(struct naaice_communication_context *comm_ctx,
                            uint8_t errorcode) {
  // FM: Multiple regions can be written back already? So this is obsolete?
  // FM TODO: What if we have more than one region to return? For example
  // ping_pong example Just a reminder: POET is actual use case where we might
  // write back multiple MRs Allow multiple wrs? multiple return addresses?
  log_trace("In naaice_swnaa_write_data\n");

  // Update state.
  comm_ctx->state = NAAICE_DATA_SENDING;

  // If there are no memory regions to write back, return an error.
  if (comm_ctx->no_local_mrs < 1) {
    log_error("No local memory regions to write back.\n");
    return -1;
  }

  // If an error occured during the NAA routine computation, send an
  // error message to the host.
  // This error message consists simply of the first byte from the first memory
  // region, because a 0 byte transfer is not possible.
  // The immediate value signifies an error by being nonzero.
  if (errorcode) {

    log_error("Error occured during NAA routine computation: %d.\n", errorcode);

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
    // FM: Multiple regions can be written back already? So this is obsolete?
    //  TODO: Include multiple return regions in the future?
    wr.wr.rdma.remote_addr =
        comm_ctx->mr_peer_data[comm_ctx->mr_return_idx].addr;
    wr.wr.rdma.rkey = comm_ctx->mr_peer_data[comm_ctx->mr_return_idx].rkey;

    // Post the send.
    int post_result = ibv_post_send(comm_ctx->qp, &wr, &bad_wr);
    if (post_result) {
      log_error("Posting send for data write "
                "(while sending error message) failed with error %d.\n",
                post_result);
      return post_result;
    }
  }

  // Otherwise, write back all memory regions specified as output parameters by
  // the to_write flag in the local memory region info struct. This can be set
  // using naaice_swnaa_set_output_mr.
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
    for (int i = 0; (i < comm_ctx->no_local_mrs) && (mr_idx < n_output_mrs);
         i++) {

      if (comm_ctx->mr_local_data[i].to_write) {

        log_debug("output mr %d (local index %d):\n", mr_idx, i);
        memset(&wr[mr_idx], 0, sizeof(wr[mr_idx]));

        wr[mr_idx].wr_id = mr_idx + 1;
        wr[mr_idx].sg_list = &sge[mr_idx];
        wr[mr_idx].num_sge = 1;

        wr[mr_idx].wr.rdma.remote_addr = comm_ctx->mr_peer_data[i].addr;
        wr[mr_idx].wr.rdma.rkey = comm_ctx->mr_peer_data[i].rkey;

        // If this is the last memory region to be written, do a write with
        // immediate. The immediate value is simply 0.
        // Otherwise, do a normal write.
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
      log_error("Posting send for data write "
                "failed with error %d.\n",
                post_result);
      return post_result;
    }
  }

  // Update state.
  // FM: No update if we want to support multiple rpc invokes
  // comm_ctx->state = FINISHED;

  return 0;
}

int naaice_swnaa_disconnect_and_cleanup(
    struct naaice_communication_context *comm_ctx) {
  // FM TODO: Fix clean up: look if stuff to clean up can be derived by
  // communication state

  log_trace("In naaice_swnaa_disconnect_and_cleanup\n");

  // Logic slightly different than on the host side:
  // All local memory regions can be freed because they do not exist in user
  // memory space.

  // Disconnect: Done by client exclusively
  // rdma_disconnect(comm_ctx->id);

  // Deregister memory regions.
  int err = 0;
  err = ibv_dereg_mr(comm_ctx->mr_local_message->ibv);
  if (err) {
    log_error("Deregestering local message memory region failed with "
              "error %d.\n",
              err);
    return -1;
  }
  for (int i = 0; i < comm_ctx->no_local_mrs; i++) {
    err = ibv_dereg_mr(comm_ctx->mr_local_data[i].ibv);
    if (err) {
      log_error("Deregestering local data memory region failed with "
                "error %d.\n",
                err);
      return -1;
    }
    free((void *)(comm_ctx->mr_local_data[i].addr));
  }

  if (comm_ctx->state >= NAAICE_MRSP_DONE) {
    free(comm_ctx->mr_peer_data);
  }

  free((void *)(comm_ctx->mr_local_message->addr));
  free(comm_ctx->mr_local_message);
  free(comm_ctx->mr_local_data);

  // Destroy queue pair.
  err = ibv_destroy_qp(comm_ctx->qp);
  if (err) {
    log_error("Destroying queue pair failed with error %d.\n", err);
    return -1;
  }

  // Destroy completion queue.
  err = ibv_destroy_cq(comm_ctx->cq);
  if (err) {
    log_error("Destroying completion queue failed with "
              "error %d.\n",
              err);
    return -1;
  }

  // Destroy completion channel.
  err = ibv_destroy_comp_channel(comm_ctx->comp_channel);
  if (err) {
    log_error("Destroying completion channel failed with "
              "error %d.\n",
              err);
    return -1;
  }

  // Destroy protection domain.
  err = ibv_dealloc_pd(comm_ctx->pd);
  if (err) {
    log_error("Destroying protection domain failed with "
              "error %d.\n",
              err);
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

int naaice_swnaa_disconnect_and_cleanup_multi(
    struct naaice_communication_context *comm_ctx) {
  // FM TODO: Fix clean up: look if stuff to clean up can be derived by
  // communication state

  log_trace("In naaice_swnaa_disconnect_and_cleanup\n");

  // Logic slightly different than on the host side:
  // All local memory regions can be freed because they do not exist in user
  // memory space.

  // Disconnect: Done by client exclusively
  // rdma_disconnect(comm_ctx->id);

  // Deregister memory regions.
  int err = 0;
  err = ibv_dereg_mr(comm_ctx->mr_local_message->ibv);
  if (err) {
    log_error("Deregestering local message memory region failed with "
              "error %d.\n",
              err);
    return -1;
  }
  for (int i = 0; i < comm_ctx->no_local_mrs; i++) {
    err = ibv_dereg_mr(comm_ctx->mr_local_data[i].ibv);
    if (err) {
      log_error("Deregestering local data memory region failed with "
                "error %d.\n",
                err);
      return -1;
    }
    free((void *)(comm_ctx->mr_local_data[i].addr));
  }

  if (comm_ctx->state >= NAAICE_MRSP_DONE) {
    free(comm_ctx->mr_peer_data);
    comm_ctx->mr_peer_data = NULL;
  }

  free((void *)(comm_ctx->mr_local_message->addr));
  free(comm_ctx->mr_local_message);
  free(comm_ctx->mr_local_data);

  // Destroy queue pair.
  err = ibv_destroy_qp(comm_ctx->qp);
  if (err) {
    log_error("Destroying queue pair failed with error %d.\n", err);
    return -1;
  }

  // Destroy completion queue.
  err = ibv_destroy_cq(comm_ctx->cq);
  if (err) {
    log_error("Destroying completion queue failed with "
              "error %d.\n",
              err);
    return -1;
  }

  // Destroy completion channel.
  err = ibv_destroy_comp_channel(comm_ctx->comp_channel);
  if (err) {
    log_error("Destroying completion channel failed with "
              "error %d.\n",
              err);
    return -1;
  }

  // Destroy protection domain.
  err = ibv_dealloc_pd(comm_ctx->pd);
  if (err) {
    log_error("Destroying protection domain failed with "
              "error %d.\n",
              err);
    return -1;
  }

  free(comm_ctx);

  return 0;
}
