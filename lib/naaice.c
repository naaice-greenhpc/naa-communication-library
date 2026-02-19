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
 * naaice.c
 *
 * Implementations for functions in naaice.h.
 *
 * Florian Mikolajczak, florian.mikolajczak@uni-potsdam.de
 * Dylan Everingham, everingham@zib.de
 * Hannes Signer, signer@uni-potsdam.de
 *
 * 26-01-2024
 *
 *****************************************************************************/

/* Dependencies **************************************************************/
#include "naaice.h"

/* Constants *****************************************************************/
#define TIMEOUT_RESOLVE_ADDR 100
#define CONNECTION_PORT 12345
#define NAA_PAGE_WIDTH 4096
#define START_RPC_MASK 0x80

/* Helper Functions **********************************************************/

// Returns a string representing a work completion opcode.
// Used in debugging.
const char *get_ibv_wc_opcode_str(enum ibv_wc_opcode opcode) {
  switch (opcode) {
  case IBV_WC_SEND:
    return "IBV_WC_SEND";
  case IBV_WC_RDMA_WRITE:
    return "IBV_WC_RDMA_WRITE";
  case IBV_WC_RDMA_READ:
    return "IBV_WC_RDMA_READ";
  case IBV_WC_COMP_SWAP:
    return "IBV_WC_COMP_SWAP";
  case IBV_WC_FETCH_ADD:
    return "IBV_WC_FETCH_ADD";
  case IBV_WC_BIND_MW:
    return "IBV_WC_BIND_MW";
  case IBV_WC_RECV:
    return "IBV_WC_RECV";
  case IBV_WC_RECV_RDMA_WITH_IMM:
    return "IBV_WC_RECV_RDMA_WITH_IMM";
  default:
    return "Unhandled Opcode";
  }
}

// Returns a string representing a naaice connection state.
// Used in debugging.
const char *get_state_str(naaice_communication_state state) {
  switch (state) {
  case NAAICE_INIT:
    return "NAAICE_INIT";
  case NAAICE_READY:
    return "NAAICE_READY";
  case NAAICE_CONNECTED:
    return "NAAICE_CONNECTED";
  case NAAICE_MRSP_SENDING:
    return "NAAICE_MRSP_SENDING";
  case NAAICE_MRSP_RECEIVING:
    return "NAAICE_MRSP_RECEIVING";
  case NAAICE_MRSP_DONE:
    return "NAAICE_MRSP_DONE";
  case NAAICE_DATA_SENDING:
    return "NAAICE_DATA_SENDING";
  case NAAICE_CALCULATING:
    return "NAAICE_CALCULATING";
  case NAAICE_DATA_RECEIVING:
    return "NAAICE_DATA_RECEIVING";
  case NAAICE_FINISHED:
    return "NAAICE_FINISHED";
  case NAAICE_ERROR:
    return "NAAICE_ERROR";
  default:
    return "Unknown State";
  }
}

void alarm_handler(int signo) {
  ulog_warn("Timeout reached. Signal: %d\n", signo);
}

// Used to generate dummy requested NAA memory region addresses.
// Will eventually be replaced by a request to the memory management service.
// Addresses start at 0 and are sequencial in the memory region sizes.
void get_sequential_naa_addresses(unsigned int n_mrs, uint64_t offset,
                                  size_t *mr_sizes,
                                  uint64_t *sequential_addrs) {

  // Set addresses.
  uint64_t curr_addr = offset;
  for (unsigned int i = 0; i < n_mrs; i++) {
    sequential_addrs[i] = curr_addr;
    curr_addr += ((mr_sizes[i] + NAA_PAGE_WIDTH - 1) / NAA_PAGE_WIDTH) *
                 NAA_PAGE_WIDTH; // align to NAA page width
  }
}

/* Function Implementations **************************************************/

int naaice_init_communication_context(
    struct naaice_communication_context **comm_ctx, uint64_t addr_offset,
    size_t *param_sizes, char **params, unsigned int params_amount,
    unsigned int internal_mr_amount, size_t *internal_mr_sizes, uint8_t fncode,
    const char *local_ip, const char *remote_ip, uint16_t port) {

  ulog_trace("In naaice_init_communication_context\n");

  // Check function code: should be positive.
  if (fncode < 1) {
    ulog_error("Function code should be positive.\n");
    return -1;
  }

  // Allocate memory for the communication context.
  *comm_ctx = (struct naaice_communication_context *)malloc(
      sizeof(struct naaice_communication_context));
  if (*comm_ctx == NULL) {
    ulog_error("Failed to allocate memory for communication context.\n");
    return -1;
  }

  // Make an event channel, checking for allocation success.
  (*comm_ctx)->ev_channel = rdma_create_event_channel();
  if (!(*comm_ctx)->ev_channel) {
    ulog_error("Failed to create an RDMA event channel.\n");
    return -1;
  }

  // Make a communication ID, checking for allocation success.
  struct rdma_cm_id *rdma_comm_id;
  if (rdma_create_id((*comm_ctx)->ev_channel, &rdma_comm_id, NULL,
                     RDMA_PS_TCP) == -1) {
    ulog_error("Failed to create an RDMA communication id.\n");
    return -1;
  }

  // Initialize fields of the communication context.
  (*comm_ctx)->state = NAAICE_INIT;
  (*comm_ctx)->id = rdma_comm_id;
  (*comm_ctx)->no_local_mrs = params_amount; // Symmetrical memory regions, so
  (*comm_ctx)->no_peer_mrs = params_amount;  // local and remote number same.
  (*comm_ctx)->no_internal_mrs = 0;
  (*comm_ctx)->mr_return_idx = 0;
  (*comm_ctx)->rdma_writes_done = 0;
  (*comm_ctx)->fncode = fncode;
  (*comm_ctx)->no_input_mrs = 0;
  (*comm_ctx)->no_output_mrs = 0;
  (*comm_ctx)->immediate = 0;
  (*comm_ctx)->bytes_received = 0;
  (*comm_ctx)->naa_returncode = 0;
  (*comm_ctx)->no_rpc_calls = 0;
  (*comm_ctx)->timeout = DEFAULT_TIMEOUT;
  (*comm_ctx)->retry_count = DEFAULT_RETRY_COUNT;

  // Dummy NAA addresses, sequential and starting at 0.
  // TODO: do this with a call to the memory mangement service.
  uint64_t naa_addresses[internal_mr_amount + params_amount];
  size_t mr_sizes[internal_mr_amount + params_amount];
  for (unsigned int i = 0; i < internal_mr_amount; i++) {
    mr_sizes[i] = internal_mr_sizes[i];
  }
  for (unsigned int i = 0; i < params_amount; i++) {
    mr_sizes[i + internal_mr_amount] = param_sizes[i];
  }
  get_sequential_naa_addresses(internal_mr_amount + params_amount, addr_offset,
                               mr_sizes, naa_addresses);

  // Initialize structs which hold information about internal memory regions,
  // i.e. those used internally on NAA for calculation.
  if (naaice_set_internal_mrs(*comm_ctx, internal_mr_amount, naa_addresses,
                              mr_sizes)) {
    return -1;
  }

  // Initialize structs which hold information about memory regions which
  // correspond to parameters.
  if (naaice_set_parameter_mrs(*comm_ctx, params_amount, (uint64_t *)params,
                               &naa_addresses[internal_mr_amount],
                               &mr_sizes[internal_mr_amount])) {
    return -1;
  }

  // Set immediate value which will be sent later as part of the data transfer.
  uint8_t *imm_bytes = (uint8_t *)calloc(3, sizeof(uint8_t));
  if (naaice_set_immediate(*comm_ctx, imm_bytes)) {
    return -1;
  }

  // Initialize memory region used to construct messages sent during MRSP.
  // Currently this is a fixed size region, the size of an advertisement +
  // request message.
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

  // port can't be larger than 65535, so 5+1 chars are sufficient here
  char port_str[6];
  snprintf(port_str, sizeof(port_str) / sizeof(port_str[0]), "%u", port);

  // Get remote address from getaddrinfo.
  struct addrinfo *rem_addr = NULL;
  if (getaddrinfo(remote_ip, port_str, NULL, &rem_addr)) {
    ulog_error("Failed to get address info for remote address.\n");
    return -1;
  }

  int resolve_addr_result = 0;

  // Get local address from getaddrinfo, if provided.
  if (local_ip != NULL && strlen(local_ip) > 0) {
    struct addrinfo *loc_addr = NULL;
    if (getaddrinfo(local_ip, port_str, NULL, &loc_addr)) {
      ulog_error("Failed to get address info for local address.\n");
      return -1;
    }
    ulog_debug("Local IP provided.\n");
    resolve_addr_result =
        rdma_resolve_addr(rdma_comm_id, loc_addr->ai_addr, rem_addr->ai_addr,
                          TIMEOUT_RESOLVE_ADDR);

    freeaddrinfo(loc_addr);
  } else {
    ulog_debug("No local IP provided.\n");
    resolve_addr_result = rdma_resolve_addr(
        rdma_comm_id, NULL, rem_addr->ai_addr, TIMEOUT_RESOLVE_ADDR);
  }

  if (resolve_addr_result == -1) {
    ulog_error("Failed to resolve addresses (errno %d).\n", errno);
    return -1;
  }

  // Addresses no longer needed, call freeaddrinfo on each of them.
  freeaddrinfo(rem_addr);
  return 0;
}

int naaice_poll_connection_event(struct naaice_communication_context *comm_ctx,
                                 struct rdma_cm_event *ev,
                                 struct rdma_cm_event *ev_cp) {

  ulog_trace("In naaice_poll_connection_event\n");

  // HINT: changed to use poll on the event channel file descriptor, to avoid
  // blocking indefinitely
  struct pollfd poll_fd;
  poll_fd.fd = comm_ctx->ev_channel->fd;
  poll_fd.events = POLLIN;
  poll_fd.revents = 0;

  int poll_result = poll(&poll_fd, 1, POLLING_TIMEOUT);
  if (poll_result <= 0) {
    return -1;
  }

  if (!rdma_get_cm_event(comm_ctx->ev_channel, &ev)) {

    // Acking Event frees memory, therefore copy event before and use it
    // subsequently.
    memcpy(ev_cp, ev, sizeof(*ev));

    // Send an ack in response.
    rdma_ack_cm_event(ev);
    return 0;
  } else {
    return -1;
  }
}

int naaice_handle_addr_resolved(struct naaice_communication_context *comm_ctx,
                                struct rdma_cm_event *ev) {

  ulog_trace("In naaice_handle_addr_resolved\n");

  if (ev->event == RDMA_CM_EVENT_ADDR_RESOLVED) {

    // Make sure we have not already handled this event...
    if (comm_ctx->state != NAAICE_READY) {

      // Update state.
      comm_ctx->state = NAAICE_READY;

      // FM: This needs to happen before the event for route resolution
      // Dylan: Enforced this using the state machine.
      if (rdma_resolve_route(comm_ctx->id, TIMEOUT_RESOLVE_ROUTE)) {
        ulog_error("RDMA route resolution initiation failed.\n");
        return -1;
      }

      return naaice_init_rdma_resources(comm_ctx);
    }
  }

  return 0;
}

int naaice_handle_route_resolved(struct naaice_communication_context *comm_ctx,
                                 struct rdma_cm_event *ev) {

  ulog_trace("In naaice_handle_route_resolved\n");

  if (ev->event == RDMA_CM_EVENT_ROUTE_RESOLVED) {

    // Make sure that address resolution is complete.
    if (comm_ctx->state != NAAICE_READY) {

      struct rdma_cm_event ev;
      ev.event = RDMA_CM_EVENT_ADDR_RESOLVED;
      naaice_handle_addr_resolved(comm_ctx, &ev);
      // After this, state should be NAAICE_READY.
    }

    // Set connection parameters.
    struct rdma_conn_param cm_params;
    memset(&cm_params, 0, sizeof(cm_params));
    cm_params.retry_count = comm_ctx->retry_count;
    cm_params.initiator_depth = 1;
    cm_params.responder_resources = 1;
    cm_params.rnr_retry_count = 6; // 7 would be indefinite

    if (rdma_connect(comm_ctx->id, &cm_params)) {
      ulog_error("RDMA connection failed.\n");
      return -1;
    }
  }

  return 0;
}

int naaice_handle_connection_established(
    struct naaice_communication_context *comm_ctx, struct rdma_cm_event *ev) {

  ulog_trace("In naaice_handle_connection_established\n");

  if (ev->event == RDMA_CM_EVENT_ESTABLISHED) {

    // Update state.
    comm_ctx->state = NAAICE_CONNECTED;
  }
  return 0;
}

int naaice_handle_error(struct naaice_communication_context *comm_ctx,
                        struct rdma_cm_event *ev) {

  ulog_trace("In naaice_handle_error\n");

  // Returns -1 and prints an error message if the event is one of
  // the following identified error types.
  // Otherwise returns 0.

  // set error state to be able to exit upstream loop in
  // naaice_setup_connection
  // comm_ctx->state = NAAICE_ERROR;
  if (ev->event == RDMA_CM_EVENT_ADDR_ERROR) {
    comm_ctx->state = NAAICE_ERROR;
    ulog_error("RDMA address resolution failed.\n");
    return -1;
  } else if (ev->event == RDMA_CM_EVENT_ROUTE_ERROR) {
    comm_ctx->state = NAAICE_ERROR;
    ulog_error("RDMA route resolution failed.\n");
    return -1;
  } else if (ev->event == RDMA_CM_EVENT_CONNECT_ERROR) {
    comm_ctx->state = NAAICE_ERROR;
    ulog_error("Error during connection establishment.\n");
    return -1;
  } else if (ev->event == RDMA_CM_EVENT_UNREACHABLE) {
    comm_ctx->state = NAAICE_ERROR;
    ulog_error("Remote peer unreachable.\n");
    return -1;
  } else if (ev->event == RDMA_CM_EVENT_REJECTED) {
    comm_ctx->state = NAAICE_ERROR;
    ulog_error("Connection request rejected by peer.\n");
    if (ev->param.conn.private_data_len > 0) {
      ulog_error(((char *)ev->param.conn.private_data));
    }
    return -1;
  } else if (ev->event == RDMA_CM_EVENT_DEVICE_REMOVAL) {
    comm_ctx->state = NAAICE_ERROR;
    ulog_error("RDMA device was removed.\n");
    return -1;
  } else if (ev->event == RDMA_CM_EVENT_DISCONNECTED) {
    // FM What to do here? is this an error state in this case? Check what needs
    //  to be cleaned up in which state...
    //  We're not expecting disconnect at this point, so we should exit.
    ulog_error("RDMA disconnected by server.\n");
    // Keep current state to have clean clean-up: depending onstate different
    // structures have been allocated
    // comm_ctx->state = DISCONNECTED;
    // Handle disconnect.
    naaice_disconnect_and_cleanup(comm_ctx);
    return -1;
  }

  return 0;
}

int naaice_handle_other(
    __attribute__((unused)) struct naaice_communication_context *comm_ctx,
    struct rdma_cm_event *ev) {

  ulog_trace("In naaice_handle_other\n");
  ulog_error("Unknown event: %d.\n", ev->event);
  return -1;
}

int naaice_poll_and_handle_connection_event(
    struct naaice_communication_context *comm_ctx) {

  ulog_trace("In naaice_poll_and_handle_connection_event\n");

  // If we've received an event...
  struct rdma_cm_event ev;
  struct rdma_cm_event ev_cp;
  if (!naaice_poll_connection_event(comm_ctx, &ev, &ev_cp)) {
    comm_ctx->id = ev_cp.id;
    switch (ev_cp.event) {

    case RDMA_CM_EVENT_ADDR_RESOLVED:
      if (naaice_handle_addr_resolved(comm_ctx, &ev_cp)) {
        return -1;
      }
      break;
    case RDMA_CM_EVENT_ROUTE_RESOLVED:
      if (naaice_handle_route_resolved(comm_ctx, &ev_cp)) {
        return -1;
      }
      break;

    case RDMA_CM_EVENT_ESTABLISHED:
      if (naaice_handle_connection_established(comm_ctx, &ev_cp)) {
        return -1;
      }
      break;

    case RDMA_CM_EVENT_ADDR_ERROR:
    case RDMA_CM_EVENT_ROUTE_ERROR:
    case RDMA_CM_EVENT_CONNECT_ERROR:
    case RDMA_CM_EVENT_UNREACHABLE:
    case RDMA_CM_EVENT_DEVICE_REMOVAL:
    case RDMA_CM_EVENT_REJECTED:
    // TODO: Not really happy about disconnected being treated as an error event
    case RDMA_CM_EVENT_DISCONNECTED:
      if (naaice_handle_error(comm_ctx, &ev_cp)) {
        return -1;
      }
      break;

    default:
      if (naaice_handle_other(comm_ctx, &ev_cp)) {
        return -1;
      }
      break;
    }
  }
  // If we successfully handled an event (or haven't received one), success.
  return 0;
}

int naaice_setup_connection(struct naaice_communication_context *comm_ctx) {

  ulog_trace("In naaice_setup_connection\n");

  // Loop handling events and updating the completion flag until finished.
  while (comm_ctx->state < NAAICE_CONNECTED) {

    naaice_poll_and_handle_connection_event(comm_ctx);
  }
  if (comm_ctx->state != NAAICE_CONNECTED) {
    return -1;
  }
  return 0;
}

int naaice_init_rdma_resources(struct naaice_communication_context *comm_ctx) {

  ulog_trace("In naaice_init_rdma_resources\n");

  // Moved from naaice_handle_addr_resolved since server never encounters this
  // the rdma_cm_id member verbs is only set after rdma_resolve_addr or
  // rdma_resolve_route we could also just pass comm_ctx->id->verbs if thats
  // more well liked
  comm_ctx->ibv_ctx = comm_ctx->id->verbs;

  // Make a protection domain, checking for allocation success.
  ulog_debug("Making protection domain.\n");
  comm_ctx->pd = ibv_alloc_pd(comm_ctx->ibv_ctx);
  if (comm_ctx->pd == NULL) {
    ulog_error("Failed to create an RDMA protection domain.\n");
    return -1;
  }

  // Make a completion channel, checking for allocation success.
  ulog_debug("Making completion channel.\n");
  comm_ctx->comp_channel = ibv_create_comp_channel(comm_ctx->ibv_ctx);
  if (comm_ctx->comp_channel == NULL) {
    ulog_error("Failed to create an IBV completion channel.\n");
    return -1;
  }

  // Make a completion queue, checking for allocation success.
  ulog_debug("Making completion queue.\n");
  comm_ctx->cq = ibv_create_cq(comm_ctx->ibv_ctx, RX_DEPTH + 1, NULL,
                               comm_ctx->comp_channel, 0);
  if (comm_ctx->cq == NULL) {
    ulog_error("Failed to create an IBV completion queue.\n");
    return -1;
  }

  // Request completion queue notifications, checking for success.
  ulog_debug("Requesting completion queue notifications.\n");
  if (ibv_req_notify_cq(comm_ctx->cq, 1)) {
    ulog_error("Failed to request completion channel notifications "
               "on completion queue.\n");
    return -1;
  }

  // Set queue pair attributes.
  struct ibv_qp_init_attr init_attr = {
      .send_cq = comm_ctx->cq,
      .recv_cq = comm_ctx->cq,
      // Exceeding the maximum number of wrs (sometimes by a lot more than 1)
      // will lead to an ENOMEM error in ibv_post_send()
      // TODO: Maybe investigate memory foot print and choose a low number if
      // not doing data transfer performance measurement
      .cap = {.max_send_wr = RX_DEPTH,
              .max_recv_wr = RX_DEPTH,
              .max_send_sge = 32,
              .max_recv_sge = 32},
      // DEFINE COMMUNICATION TYPE!
      .qp_type = IBV_QPT_RC,
      .sq_sig_all = 1};

  int ret;
  struct ibv_device_attr device_attr;
  ret = ibv_query_device(comm_ctx->ibv_ctx, &device_attr);
  if (ret) {
    ulog_error("Failed to query device\n");
  }

  // Make a queue pair, checking for allocation success.
  ulog_debug("Making queue pair.\n");
  if (rdma_create_qp(comm_ctx->id, comm_ctx->pd, &init_attr)) {
    perror("Failed to create an RDMA queue pair.\n");
    return -1;
  }
  comm_ctx->qp = comm_ctx->id->qp;

  return 0;
}

int naaice_register_mrs(struct naaice_communication_context *comm_ctx) {

  ulog_trace("In naaice_register_mrs\n");

  // Register all memory regions corresponding to parameters and the metadata
  // memory region.
  for (int i = 0; i < ((int)comm_ctx->no_local_mrs); i++) {

    comm_ctx->mr_local_data[i].ibv =
        ibv_reg_mr(comm_ctx->pd, comm_ctx->mr_local_data[i].addr,
                   comm_ctx->mr_local_data[i].size,
                   (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));

    if (comm_ctx->mr_local_data[i].ibv == NULL) {
      ulog_error(
          "client: Failed to register memory for local memory region.\n");
      return -1;
    }
  }

  // Register the memory region used for MRSP.
  comm_ctx->mr_local_message->ibv =
      ibv_reg_mr(comm_ctx->pd, comm_ctx->mr_local_message->addr, MR_SIZE_MRSP,
                 (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));
  if (comm_ctx->mr_local_message->ibv == NULL) {
    ulog_error("Failed to register memory for memory region setup protocol.\n");
    return -1;
  }

  return 0;
}

int naaice_set_parameter_mrs(struct naaice_communication_context *comm_ctx,
                             unsigned int n_parameter_mrs,
                             uint64_t *local_addrs, uint64_t *remote_addrs,
                             size_t *sizes) {

  ulog_trace("In naaice_set_parameter_mrs\n");

  // Set number of local and peer (i.e. remote) memory regions in
  // communication context.
  // Because we use symmetrical memory regions, the number is the same
  // (we don't count internal memory regions here).
  comm_ctx->no_local_mrs = n_parameter_mrs;
  comm_ctx->no_peer_mrs = n_parameter_mrs;

  // Initialize memory to store information about local memory regions.
  comm_ctx->mr_local_data = (struct naaice_mr_local *)calloc(
      comm_ctx->no_local_mrs, sizeof(struct naaice_mr_local));
  if (!comm_ctx->mr_local_data) {
    ulog_error("Failed to allocate memory for local mr info structures.\n");
    return -1;
  }

  // Set struct fields.
  for (unsigned int i = 0; i < comm_ctx->no_local_mrs; i++) {
    comm_ctx->mr_local_data[i].size = sizes[i];
    comm_ctx->mr_local_data[i].addr = (char *)local_addrs[i];

    // The to_write field for local MRs indicates that the MR is an input
    // parameter and should be written to the NAA.
    // We initialize them all to false, but they should be updated prior to
    // the first RPC with naaice_set_input_mr.
    comm_ctx->mr_local_data[i].to_write = false;

    // The single_send field for local MRs indicates that the MR should only
    // be sent on the first RPC on this connection.
    // We initialize to false, but they should be updated prior to the first
    // RPC with naaice_set_singlesend_mr.
    comm_ctx->mr_local_data[i].single_send = false;
  }

  // Allocate memory to store information about remote memory regions.
  comm_ctx->mr_peer_data = (struct naaice_mr_peer *)calloc(
      comm_ctx->no_peer_mrs, sizeof(struct naaice_mr_peer));
  if (!comm_ctx->mr_peer_data) {
    ulog_error("Failed to allocate memory for remote mr info structures.\n");
    return -1;
  }

  // Set struct fields.
  for (unsigned int i = 0; i < comm_ctx->no_peer_mrs; i++) {
    comm_ctx->mr_peer_data[i].size = sizes[i];

    // Remote addresses (i.e. those to be requested on the NAA) must fit within
    // 7 bytes. Make sure this is the case.
    comm_ctx->mr_peer_data[i].addr = remote_addrs[i];

    // The to_write field for remote MRs indicates that the MR is an output
    // parameter and should be written back from the NAA.
    // We initialize them all to false, but they should be updated prior to
    // the first RPC with naaice_set_output_mr.
    comm_ctx->mr_peer_data[i].to_write = false;

    // The single_send field for remote MRs indicates that the MR should only
    // be expected to be sent on the first RPC on this connection.
    // We initialize to false, but they should be updated prior to the first
    // RPC with naaice_set_singlesend_mr.
    comm_ctx->mr_peer_data[i].single_send = false;

    // TODO: consider if this alignment needs to be handled by AP1 or by
    // the user. Currently, it is entirely on the user side.
    // Memory alignment isn't strictly necessary, just has better performance
    /*
    posix_memalign((void **)&(comm_ctx->mr_local_data[i].addr),
           sysconf(_SC_PAGESIZE), comm_ctx->mr_local_data[i].size);
    if (!comm_ctx->mr_local_data[i].addr) {
      ulog_error(
          "Failed to allocate local memory region buffer.\n");
      return -1;
    }
    */
  }

  return 0;
}

int naaice_set_input_mr(struct naaice_communication_context *comm_ctx,
                        unsigned int input_mr_idx) {

  ulog_trace("In naaice_set_input_mr\n");

  // Check if passed index is a valid parameter memory region index.
  if (input_mr_idx >= comm_ctx->no_local_mrs) {
    ulog_error("Tried to set invalid memory region #%d as input, there "
               "are only %d local memory regions.\n",
               input_mr_idx, comm_ctx->no_local_mrs);
    return -1;
  }

  // If the MR was not already set as input, set it so and increment the
  // number of inputs.
  if (!comm_ctx->mr_local_data[input_mr_idx].to_write) {
    comm_ctx->mr_local_data[input_mr_idx].to_write = true;
    comm_ctx->no_input_mrs++;
  }

  return 0;
}

int naaice_set_output_mr(struct naaice_communication_context *comm_ctx,
                         unsigned int output_mr_idx) {

  ulog_trace("In naaice_set_output_mr\n");

  // Check if passed index is a valid parameter memory region index.
  if (output_mr_idx >= comm_ctx->no_peer_mrs) {
    ulog_error("Tried to set invalid memory region #%d as output, there "
               "are only %d local memory regions.\n",
               output_mr_idx, comm_ctx->no_peer_mrs);
    return -1;
  }

  // If the MR was not already set as output, set it so and increment the
  // number of outputs.
  if (!comm_ctx->mr_peer_data[output_mr_idx].to_write) {
    comm_ctx->mr_peer_data[output_mr_idx].to_write = true;
    comm_ctx->no_output_mrs++;
  }

  return 0;
}

int naaice_set_singlesend_mr(struct naaice_communication_context *comm_ctx,
                             unsigned int singlesend_mr_idx) {

  ulog_trace("In naaice_set_singlesend_mr\n");

  // Check if passed index is a valid parameter memory region index.
  if (singlesend_mr_idx >= comm_ctx->no_peer_mrs) {
    ulog_error("Tried to set invalid memory region #%d as single send, "
               "there are only %d local memory regions.\n",
               singlesend_mr_idx, comm_ctx->no_peer_mrs);
    return -1;
  }

  // Set the MR as single send.
  comm_ctx->mr_local_data[singlesend_mr_idx].single_send = true;

  // If the MR was not already set as input, set it so and increment the
  // number of inputs.
  if (!comm_ctx->mr_local_data[singlesend_mr_idx].to_write) {
    comm_ctx->mr_local_data[singlesend_mr_idx].to_write = true;
    comm_ctx->no_input_mrs++;
  }

  return 0;
}

int naaice_set_internal_mrs(struct naaice_communication_context *comm_ctx,
                            unsigned int n_internal_mrs, uint64_t *addrs,
                            size_t *sizes) {

  ulog_trace("In naaice_set_internal_mrs\n");

  // Update number of internal memory regions.
  comm_ctx->no_internal_mrs = n_internal_mrs;

  // Allocate space for internal memory region info structs.
  comm_ctx->mr_internal = (struct naaice_mr_internal *)calloc(
      comm_ctx->no_internal_mrs, sizeof(struct naaice_mr_internal));

  if (!comm_ctx->mr_internal) {
    ulog_error("Failed to allocate internal memory region structures.\n");
    return -1;
  }

  // Set struct fields.
  // Addresses must fit in 7 bytes.
  for (unsigned int i = 0; i < n_internal_mrs; i++) {
    comm_ctx->mr_internal[i].addr = addrs[i];
    comm_ctx->mr_internal[i].size = sizes[i];
  }

  return 0;
}

int naaice_set_immediate(struct naaice_communication_context *comm_ctx,
                         uint8_t *imm_bytes) {

  // if user wants to use first byte for encoding information print a warning
  // since it is overwritten with the function code
  if (imm_bytes[0] != 0) {
    ulog_warn(
        "First byte of immediate value is overwritten with function code\n");
  }

  // Immediate byte array should hold no more than 3 bytes.
  // These are placed in the 3 higher bytes of the immediate value.
  for (unsigned int i = 1; i < 4; i++) {
    comm_ctx->immediate_bytearr[i] = imm_bytes[i - 1];
  }

  // First byte should always be the function code.
  comm_ctx->immediate_bytearr[0] = comm_ctx->fncode;

  return 0;
}

int naaice_init_mrsp(struct naaice_communication_context *comm_ctx) {

  ulog_trace("In naaice_init_mrsp\n");

  // TODO: handle errors from these.
  // Post receive for response before sending message. Otherwise data race.
  // Since we only get back to this level after the send is acknowledged.
  naaice_post_recv_mrsp(comm_ctx);

  // Send MRSP announce + request.
  naaice_send_message(comm_ctx, MSG_MR_AAR, 0);

  return 0;
}

int naaice_init_data_transfer(struct naaice_communication_context *comm_ctx) {

  // FM TODO: Error handling
  if (naaice_post_recv_data(comm_ctx)) {
    // we encountered some error, set fncode 0
    comm_ctx->fncode = 0;
    // If we encountered error, after sending data with fncode 0
    // we should exit connection
  }

  if (naaice_write_data(comm_ctx, comm_ctx->fncode)) {
    // there's some problem in writing data.
    // How do we tell the server? Some message on MRSP MR?
    // Enter error state and exit connection
  };

  // Increment number of RPC calls.
  comm_ctx->no_rpc_calls++;

  return 0;
}

int naaice_handle_work_completion(
    struct ibv_wc *wc, struct naaice_communication_context *comm_ctx) {

  ulog_trace("In naaice_handle_work_completion\n");

  ulog_debug("state: %s, opcode: %s\n", get_state_str(comm_ctx->state),
             get_ibv_wc_opcode_str(wc->opcode));

  // If the work completion status is not success, return with error.
  if (wc->status != IBV_WC_SUCCESS) {
    ulog_error("Status is not IBV_WC_SUCCESS. Status %d for operation %d.\n",
               wc->status, wc->opcode);
    return -1;
  }

  // If we're still sending the MRSP packet...
  if (comm_ctx->state == NAAICE_MRSP_SENDING) {

    // If we've finished sending the MRSP packet...
    if (wc->opcode == IBV_WC_SEND) {

      // Update state.
      comm_ctx->state = NAAICE_MRSP_RECEIVING;
      return 0;
    }
  }

  // If we're waiting for an MRSP response from the NAA...
  else if (comm_ctx->state == NAAICE_MRSP_RECEIVING) {

    // If we have received a write without immediate...
    if (wc->opcode == IBV_WC_RECV) {

      // Then we've recieved an MRSP message. Handle it.

      // Print all information about the work completion.
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
      ulog_debug("wc_flags: %d\n", wc->wc_flags);
      ulog_debug("slid: %d\n", wc->slid);
      ulog_debug("sl: %d\n", wc->sl);
      ulog_debug("dlid_path_bits: %d\n", wc->dlid_path_bits);
      */

      struct naaice_mr_hdr *msg =
          (struct naaice_mr_hdr *)comm_ctx->mr_local_message->addr;

      // If the message is a memory region announcment...
      if (msg->type == MSG_MR_A) {

        // printf("Received Memory region request and advertisement\n");
        struct naaice_mr_dynamic_hdr *dyn =
            (struct naaice_mr_dynamic_hdr *)(comm_ctx->mr_local_message->addr +
                                             sizeof(struct naaice_mr_hdr));

        // Record information about the NAA's memory regions from the message.
        struct naaice_mr_advertisement *mr =
            (struct naaice_mr_advertisement
                 *)(comm_ctx->mr_local_message->addr +
                    sizeof(struct naaice_mr_hdr) +
                    sizeof(struct naaice_mr_dynamic_hdr));

        for (int i = 0; i < (dyn->count); i++) {
          comm_ctx->mr_peer_data[i].addr = ntohll(mr->addr);
          comm_ctx->mr_peer_data[i].rkey = ntohl(mr->rkey);
          comm_ctx->mr_peer_data[i].size = ntohl(mr->size);
          ulog_debug("Peer MR %d: Addr: %lX, Size: %lu, rkey: %d\n", i + 1,
                     comm_ctx->mr_peer_data[i].addr,
                     comm_ctx->mr_peer_data[i].size,
                     comm_ctx->mr_peer_data[i].rkey);
          mr = (struct naaice_mr_advertisement
                    *)(comm_ctx->mr_local_message->addr +
                       (sizeof(struct naaice_mr_hdr) +
                        sizeof(struct naaice_mr_dynamic_hdr) +
                        (i + 1) * sizeof(struct naaice_mr_advertisement)));
        }

        // Update state.
        comm_ctx->state = NAAICE_MRSP_DONE;

        return 0;
      }

      // Return with error if a remote error has occured.
      else if (msg->type == MSG_MR_ERR) {
        struct naaice_mr_error *err =
            (struct naaice_mr_error *)(comm_ctx->mr_local_message->addr +
                                       sizeof(struct naaice_mr_hdr));
        ulog_error("Remote node encountered error in memory region exchange: "
                   "%x.\n",
                   err->code);
        return -1;
      }

      // Otherwise, some weird message type. Return with error.
      else {
        ulog_error("Unhandled MRSP packet type received: %d\n", msg->type);
        return -1;
      }
    }
  }

  // If we're sending data...
  else if (comm_ctx->state == NAAICE_DATA_SENDING) {

    // If we've written some data...
    if (wc->opcode == IBV_WC_RDMA_WRITE) {

      // Increment the number of completed writes.
      comm_ctx->rdma_writes_done++;

      // TODO FM: This might change later to the number we actually send
      // If all writes have been completed, start waiting for the response.
      if (comm_ctx->rdma_writes_done == comm_ctx->no_input_mrs) {

        // Update state.
        // We skip straight to NAAICE_DATA_RECEIVING; the NAAICE_CALCULATING
        // state is used only by the software NAA.
        comm_ctx->state = NAAICE_DATA_RECEIVING;
        comm_ctx->rdma_writes_done = 0;
      }

      return 0;
    }

  }

  // If we're receiving data...
  else if (comm_ctx->state == NAAICE_DATA_RECEIVING) {

    // FM: Technically, a Write from the client does not trigger a work
    // completion on the server If we received data without an immediate...
    if (wc->opcode == IBV_WC_RECV) {

      // No need to do anything.
      return 0;
    }

    // If we have received data with an immediate...
    if (wc->opcode == IBV_WC_RECV_RDMA_WITH_IMM) {

      uint32_t return_code = ntohl(wc->imm_data) >> IMMEDIATE_OFFSET;
      // Check whether an error occured.
      if (return_code) {
        ulog_error("Immediate return code non-zero: %d.\n", return_code);
        return return_code;
      }

      // Print all information about the work completion.
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
      ulog_debug("wc_flags: %d\n", wc->wc_flags);
      ulog_debug("slid: %d\n", wc->slid);
      ulog_debug("sl: %d\n", wc->sl);
      ulog_debug("dlid_path_bits: %d\n", wc->dlid_path_bits);
      */

      // If no error, go to finished state.
      comm_ctx->state = NAAICE_FINISHED;
      comm_ctx->naa_returncode = wc->imm_data;
      comm_ctx->bytes_received += wc->byte_len;
      return 0;
    }
  }

  // If we've reached this point, the work completion had an opcode which is
  // not handled for the current state, so return with error.
  ulog_warn(
      "Work completion opcode (wc opcode): %s, not handled for state: %s.\n",
      get_ibv_wc_opcode_str(wc->opcode), get_state_str(comm_ctx->state));
  return -1;
}

int naaice_poll_cq_blocking(struct naaice_communication_context *comm_ctx) {
  ulog_trace("In naaice_poll_cq_blocking\n");

  // signal handler to terminate blocking call after timeout
  struct sigaction sa;
  sa.sa_handler = alarm_handler;
  sa.sa_flags = 0; // SA_RESTART is not set
  sigemptyset(&sa.sa_mask);
  sigaction(SIGALRM, &sa, NULL);
  alarm((int)comm_ctx->timeout);

  struct ibv_cq *ev_cq;
  void *ev_ctx;

  // Ensure completion channel is in blocking mode.
  int fd_flags = fcntl(comm_ctx->comp_channel->fd, F_GETFL);
  if (fcntl(comm_ctx->comp_channel->fd, F_SETFL, fd_flags & ~O_NONBLOCK) < 0) {
    ulog_error("Failed to change file descriptor of completion event "
               "channel.\n");
    return -1;
  }

  // If something is received, get the completion event.
  if (ibv_get_cq_event(comm_ctx->comp_channel, &ev_cq, &ev_ctx)) {
    ulog_error("Failed to get completion queue event.\n");
    if (naaice_disconnect_and_cleanup(comm_ctx)) {
      ulog_error("Error in cleanup procedure.\n");
    };
    return -1;
  }

  // Ack the completion event.
  ibv_ack_cq_events(ev_cq, 1);

  // While there are work completions in the completion queue, handle them.
  struct ibv_wc wc;
  int n_wcs = ibv_poll_cq(comm_ctx->cq, 1, &wc);

  // If ibv_poll_cq returns an error, return.
  if (n_wcs < 0) {
    ulog_error("ibv_poll_cq() failed.\n");
    return -1;
  }

  while (n_wcs) {

    // Handle the work completion.
    if (naaice_handle_work_completion(&wc, comm_ctx)) {
      ulog_error("Error while handling work completion.\n");
      return -1;
    }

    // Find any remaining work completions in the queue.
    n_wcs = ibv_poll_cq(comm_ctx->cq, 1, &wc);
    if (n_wcs < 0) {
      ulog_error("ibv_poll_cq() failed.\n");
      return -1;
    }
  }

  // Request completion channel notifications for the next event.
  if (ibv_req_notify_cq(comm_ctx->cq, 1)) {
    ulog_error(
        "Failed to request completion channel notifications on completion "
        "queue.\n");
    return -1;
  }

  return 0;
}

int naaice_poll_cq_nonblocking(struct naaice_communication_context *comm_ctx) {

  ulog_trace("In naaice_poll_cq_nonblocking\n");

  struct ibv_cq *ev_cq;
  void *ev_ctx;

  // Ensure completion channel is in non-blocking mode.
  int fd_flags = fcntl(comm_ctx->comp_channel->fd, F_GETFL);
  if (fcntl(comm_ctx->comp_channel->fd, F_SETFL, fd_flags | O_NONBLOCK) < 0) {
    ulog_error("Failed to change file descriptor of completion event "
               "channel.\n");
    return -1;
  }

  struct pollfd my_pollfd;
  // Poll the completion channel, returning with flag unchanged if nothing
  // is received.
  my_pollfd.fd = comm_ctx->comp_channel->fd;
  my_pollfd.events = POLLIN;
  my_pollfd.revents = 0;

  // Nonblocking: if poll times out, just return.
  int poll_result = poll(&my_pollfd, 1, POLLING_TIMEOUT);
  if (poll_result < 0) {
    // FM: This is probably an error. If none is received, we get back 0.
    ulog_error("Error occurred when polling completion channel.\n");
    return -1;
  } else if (poll_result == 0) {

    // We have simply not recieved any events.
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
  int n_wcs = ibv_poll_cq(comm_ctx->cq, 1, &wc);

  // If ibv_poll_cq returns an error, return.
  if (n_wcs < 0) {
    ulog_error("ibv_poll_cq() failed.\n");
    return -1;
  }

  while (n_wcs) {
    ulog_debug("number of work completions: %d\n", n_wcs);
    // Handle the work completion.
    if (naaice_handle_work_completion(&wc, comm_ctx)) {
      ulog_error("Error while handling work completion.\n");
      return -1;
    }

    // Find any remaining work completions in the queue.
    n_wcs = ibv_poll_cq(comm_ctx->cq, 1, &wc);
    if (n_wcs < 0) {
      ulog_error("ibv_poll_cq() failed.\n");
      return -1;
    }
  }
  // FM TODO: We need the solicited event to ensure blocking behavior? We don't
  // want to be woken up by our own successful write request comletions but only
  // by receiving and rmda_with_imm Request completion channel notifications for
  // the next event.
  if (ibv_req_notify_cq(comm_ctx->cq, 1)) {
    ulog_error(

        "Failed to request completion channel notifications on completion "
        "queue.\n");
    return -1;
  }

  return 0;
}

int naaice_do_mrsp(struct naaice_communication_context *comm_ctx) {

  ulog_trace("In naaice_do_mrsp\n");

  // Initialize the MRSP.
  if (naaice_init_mrsp(comm_ctx)) {
    return -1;
  }

  // Poll the completion queue and handle work completions until the MRSP is
  // complete.
  time_t start, end;
  time(&start);
  while (comm_ctx->state < NAAICE_MRSP_DONE) {
    time(&end);
    if (naaice_poll_cq_nonblocking(comm_ctx)) {
      return -1;
    }
    if (comm_ctx->timeout > 0 && difftime(end, start) > comm_ctx->timeout) {
      ulog_error(
          "naaice_do_mrsp: Timeout while waiting for a response from the "
          "NAA (max timeout %fs)\n",
          comm_ctx->timeout);
      return -1;
    }
  }

  return 0;
}

int naaice_do_data_transfer(struct naaice_communication_context *comm_ctx) {

  ulog_trace("In naaice_do_data_transfer\n");

  // Initialize the data transfer.
  if (naaice_init_data_transfer(comm_ctx)) {
    return -1;
  }

  // Poll the completion queue and handle work completions until data transfer,
  // including sending, calculation, and receiving, is complete.
  time_t start, end;
  time(&start);
  while (comm_ctx->state < NAAICE_FINISHED) {
    time(&end);
    if (naaice_poll_cq_nonblocking(comm_ctx)) {
      return -1;
    }
    if (comm_ctx->timeout > 0 && difftime(end, start) > comm_ctx->timeout) {
      ulog_error(
          "naaice_do_data_transfer: Timeout while waiting for a response "
          "from the NAA (max timeout %fs) current state: %s \n",
          comm_ctx->timeout, get_state_str(comm_ctx->state));
      return -1;
    }
  }

  return 0;
}

int naaice_disconnect_and_cleanup(
    struct naaice_communication_context *comm_ctx) {
  // FM TODO: Fix clean up: look if stuff to clean up can be derived by
  // communication state
  ulog_trace("In naaice_disconnect_and_cleanup\n");

  // Disconnect.
  rdma_disconnect(comm_ctx->id);

  // Deregister memory regions.
  int err = 0;
  err = ibv_dereg_mr(comm_ctx->mr_local_message->ibv);
  if (err) {
    ulog_error("Deregestering local message memory region failed with "
               "error %d.\n",
               err);
    return -1;
  }
  for (int i = 0; i < comm_ctx->no_local_mrs; i++) {
    err = ibv_dereg_mr(comm_ctx->mr_local_data[i].ibv);
    if (err) {
      ulog_error("Deregestering local data memory region failed with "
                 "error %d.\n",
                 err);
      return -1;
    }
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
    ulog_error("Destroying queue pair failed with error %d.\n", err);
    return -1;
  }

  // Destroy completion queue.
  err = ibv_destroy_cq(comm_ctx->cq);
  if (err) {
    ulog_error("Destroying completion queue failed with "
               "error %d.\n",
               err);
    return -1;
  }

  // Destroy completion channel.
  err = ibv_destroy_comp_channel(comm_ctx->comp_channel);
  if (err) {
    ulog_error("Destroying completion channel failed with "
               "error %d.\n",
               err);
    return -1;
  }

  // Destroy protection domain.
  err = ibv_dealloc_pd(comm_ctx->pd);
  if (err) {
    ulog_error("Destroying protection domain failed with "
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

int naaice_send_message(struct naaice_communication_context *comm_ctx,
                        enum message_id message_type, uint8_t errorcode) {

  ulog_trace("In naaice_send_message\n");

  // Update state.
  comm_ctx->state = NAAICE_MRSP_SENDING;

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

  // If we're sending an advertisement + request message (for MRSP)...
  if (msg->type == MSG_MR_AAR) {

    // Add a dynamic header.
    struct naaice_mr_dynamic_hdr *dyn =
        (struct naaice_mr_dynamic_hdr *)(msg + sizeof(struct naaice_mr_hdr));
    msg_size += sizeof(struct naaice_mr_dynamic_hdr);
    dyn->padding[0] = 0;
    dyn->padding[1] = 0;

    // Number of advertised memory regions is the number of parameters,
    // plus one for the metadata memory region (included in no_local_mrs),
    // plus the number of internal memory regions to be used by the NAA.
    dyn->count = comm_ctx->no_local_mrs + comm_ctx->no_internal_mrs;

    // Pointer to the current position in the message being constructed.
    struct naaice_mr_advertisement_request *curr;

    // For each parameter memory region...
    for (int i = 0; i < comm_ctx->no_local_mrs; i++) {

      // Point to next position in the packet.
      curr = (struct naaice_mr_advertisement_request
                  *)(msg + sizeof(struct naaice_mr_hdr) +
                     sizeof(struct naaice_mr_dynamic_hdr) +
                     i * sizeof(struct naaice_mr_advertisement_request));

      // Set fields of the packet.
      curr->addr = htonll((uintptr_t)comm_ctx->mr_local_data[i].addr);
      curr->size = htonl(comm_ctx->mr_local_data[i].ibv->length);
      curr->rkey = htonl(comm_ctx->mr_local_data[i].ibv->rkey);
      curr->mr_info_bytearray[7] = 0;

      // MR flag may indicate if the region should only be sent once.
      if (comm_ctx->mr_local_data[i].single_send) {
        curr->mr_info_bytearray[7] |= MRFLAG_SINGLESEND;
      }

      // MR flag may indicate if MR is an input.
      if (comm_ctx->mr_local_data[i].to_write) {
        curr->mr_info_bytearray[7] |= MRFLAG_INPUT;
      }

      // MR flag may indicate if MR is an output.
      if (comm_ctx->mr_peer_data[i].to_write) {
        curr->mr_info_bytearray[7] |= MRFLAG_OUTPUT;
      }

      // During set_parameter_mrs, the peer memory region addresses are checked
      // to be sure they fit into 7 bytes.
      for (int j = 0; j < 7; j++) {
        curr->mr_info_bytearray[j] = comm_ctx->mr_peer_data[i].fpgaaddr[j];
      }
      curr->mr_info = htonll(curr->mr_info);

      ulog_debug("Local MR %d: Addr: %lX, Size: %ld, rkey: %d, Requested NAA "
                 "Addr: %lX\n",
                 i + 1, (uintptr_t)comm_ctx->mr_local_data[i].addr,
                 comm_ctx->mr_local_data[i].ibv->length,
                 comm_ctx->mr_local_data[i].ibv->rkey,
                 comm_ctx->mr_peer_data[i].addr);

      // Update packet size.
      msg_size += sizeof(struct naaice_mr_advertisement_request);
    }

    // For each internal memory region...
    for (int i = 0; i < comm_ctx->no_internal_mrs; i++) {

      // Point to next position in the packet.
      curr = (struct naaice_mr_advertisement_request
                  *)(msg + sizeof(struct naaice_mr_hdr) +
                     sizeof(struct naaice_mr_dynamic_hdr) +
                     comm_ctx->no_local_mrs *
                         sizeof(struct naaice_mr_advertisement_request) +
                     i * sizeof(struct naaice_mr_advertisement_request));

      // Set fields of the packet.
      // Internal memory regions only exist on the NAA, so local address here
      // is just 0. No rkey is required, so that's also just 0.
      curr->addr = htonll(0);
      curr->size = htonl(comm_ctx->mr_internal[i].size);
      curr->rkey = htonl(0);
      curr->mr_info_bytearray[7] = MRFLAG_INTERNAL;

      // During set_internal_mrs, the peer memory region addresses are checked
      // to be sure they fit into 7 bytes.
      for (int j = 0; j < 7; j++) {
        curr->mr_info_bytearray[j] = comm_ctx->mr_internal[i].fpgaaddr[j];
      }
      curr->mr_info = htonll(curr->mr_info);

      ulog_debug(
          "Internal MR %d: Addr: %lX, Size: %d, Requested NAA Addr: %lX\n",
          i + 1, (uintptr_t)comm_ctx->mr_internal[i].addr,
          (int)comm_ctx->mr_internal[i].size, comm_ctx->mr_internal[i].addr);

      // Update packet size.
      msg_size += sizeof(struct naaice_mr_advertisement_request);
    }
  }

  // If we're sending an error message...
  if (message_type == MSG_MR_ERR) {

    // Insert error packet. No dynamic header for this message type.
    struct naaice_mr_error *err =
        (struct naaice_mr_error *)(msg + sizeof(struct naaice_mr_hdr));

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
  // wr.send_flags = IBV_SEND_SIGNALED;
  wr.send_flags = IBV_SEND_SOLICITED;

  // Send the packet.
  int post_result = ibv_post_send(comm_ctx->qp, &wr, &bad_wr);
  if (post_result) {
    ulog_error("Posting send for MRSP failed with error %d.\n", post_result);
    return -1;
  }

  return 0;
}

int naaice_set_bytes_to_send(struct naaice_communication_context *comm_ctx,
                             int mr_idx, int number_bytes) {

  if (mr_idx > comm_ctx->no_local_mrs - 1) {
    ulog_error("Index of memory region is out if bounds!\n");
    return -1;
  }

  if (comm_ctx->mr_local_data[mr_idx].ibv == NULL) {
    ulog_error("Memory regions are not yet registered. Please call "
               "function after naaice_register_mrs!\n");
    return -1;
  }

  // reset number of bytes size of the memory region
  if (number_bytes < 0) {
    comm_ctx->mr_local_data[mr_idx].size =
        comm_ctx->mr_local_data[mr_idx].ibv->length;
    return 0;
  }

  if ((size_t)number_bytes > comm_ctx->mr_local_data[mr_idx].ibv->length) {
    ulog_error(
        "Number of specified bytes larger than size of memory region!\n");
    return -1;
  }

  comm_ctx->mr_local_data[mr_idx].size = number_bytes;

  return 0;
}

int naaice_write_data(struct naaice_communication_context *comm_ctx,
                      uint8_t fncode) {

  ulog_trace("In naaice_write_data\n");
  ulog_debug("fncode: %d\n", fncode);

  // Update state.
  comm_ctx->state = NAAICE_DATA_SENDING;

  // If provided function code is zero, send a write indicating a host-side
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
      ulog_error("Posting send for data write "
                 "(while sending error message) failed with error %d.\n",
                 post_result);
      return post_result;
    }
  }

  // Otherwise, write all memory regions indicated as input regions with the
  // to_write flag. This can be updated with naaice_set_input_mr.
  else {

    // Remove single send regions from regions to be sent after the first RPC
    // call
    for (unsigned int i = 0; i < comm_ctx->no_local_mrs; i++) {
      if (comm_ctx->mr_local_data[i].single_send &&
          comm_ctx->mr_local_data[i].to_write && (comm_ctx->no_rpc_calls > 0)) {
        comm_ctx->mr_local_data[i].to_write = false;
        comm_ctx->no_input_mrs--;
      }
    }

    // We will have one write request (and one scatter/gather elements) for
    // each memory region to be written.
    struct ibv_send_wr wr[comm_ctx->no_input_mrs], *bad_wr = NULL;
    struct ibv_sge sge[comm_ctx->no_input_mrs];

    // Construct write requests and scatter/gather elements for all memory
    // regions to be sent.
    uint8_t mr_idx = 0;
    for (int i = 0;
         (i < comm_ctx->no_local_mrs) && (mr_idx < comm_ctx->no_input_mrs);
         i++) {

      if (comm_ctx->mr_local_data[i].to_write) {
        memset(&wr[mr_idx], 0, sizeof(wr[mr_idx]));
        wr[mr_idx].wr_id = mr_idx + 1;
        wr[mr_idx].sg_list = &sge[mr_idx];
        wr[mr_idx].num_sge = 1;
        wr[mr_idx].wr.rdma.remote_addr = comm_ctx->mr_peer_data[i].addr;
        wr[mr_idx].wr.rdma.rkey = comm_ctx->mr_peer_data[i].rkey;

        // If this is the last memory region to be written, do a write with
        // immediate. The immediate value holds the function code as well as
        // 24 bits configured using naaice_set_immediate.
        // Otherwise, do a normal write.
        // We set the 8th bit to 1 in order to indicate host to NAA
        // transmission.
        if (mr_idx == comm_ctx->no_input_mrs - 1) {
          wr[mr_idx].opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
          wr[mr_idx].send_flags = IBV_SEND_SOLICITED;
          wr[mr_idx].imm_data = htonl(comm_ctx->immediate | START_RPC_MASK);
          wr[mr_idx].next = NULL;
        } else {
          wr[mr_idx].opcode = IBV_WR_RDMA_WRITE;
          wr[mr_idx].next = &wr[mr_idx + 1];
        }

        sge[mr_idx].addr = (uintptr_t)comm_ctx->mr_local_data[i].addr;
        sge[mr_idx].length = comm_ctx->mr_local_data[i].size;
        sge[mr_idx].lkey = comm_ctx->mr_local_data[i].ibv->lkey;

        mr_idx++;
      }
    }

    // Send the write.
    int post_result = ibv_post_send(comm_ctx->qp, &wr[0], &bad_wr);
    if (post_result) {
      ulog_error("Posting send for data write failed with error %d.\n",
                 post_result);
      return post_result;
    }
  }

  return 0;
}

int naaice_post_recv_mrsp(struct naaice_communication_context *comm_ctx) {

  ulog_debug("In naaice_post_recv_mrsp\n");

  // Construct the receive request and scatter/gather elements.
  struct ibv_recv_wr wr, *bad_wr = NULL;
  struct ibv_sge sge;

  sge.addr = (uintptr_t)comm_ctx->mr_local_message->addr;
  sge.length = MR_SIZE_MRSP;
  sge.lkey = comm_ctx->mr_local_message->ibv->lkey;

  memset(&wr, 0, sizeof(wr));
  wr.wr_id = 1;
  wr.next = NULL;
  wr.sg_list = &sge;
  wr.num_sge = 1;

  // Post the receive.
  int post_result = ibv_post_recv(comm_ctx->qp, &wr, &bad_wr);
  if (post_result) {
    ulog_error("Posting receive for MRSP failed with error %d.\n", post_result);
    return post_result;
  }

  return 0;
}

int naaice_post_recv_data(struct naaice_communication_context *comm_ctx) {

  /**
   * \note We only have to post one receive request for the data transfer, since
   * we only get one completion queue element for an immediate write of the
   * server application. Posting multiple receive requests will lead to errors
   * when using more than one output memory region with a large amount of RPC
   * calls.  This happends due to an overflow of the receive queue.
   */

  ulog_info("In naaice_post_recv_data\n");

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

  // Post the recieve.
  int post_result = ibv_post_recv(comm_ctx->qp, &wr, &bad_wr);
  if (post_result) {
    ulog_error("Posting recieve for data failed with error %d.\n", post_result);
    return post_result;
  }

  return 0;
}
