/* Dependencies **************************************************************/
#ifdef REMOTE_MEMORY_MANAGEMENT_SERVICE
#include <curl/curl.h>

#include "malloc_client.h"
#endif
#include "naaice.h"

/* Constants *****************************************************************/
#define TIMEOUT_RESOLVE_ADDR 100
#define NAA_PAGE_WIDTH 4096
#define START_RPC_MASK 0x80

/* Helper Functions **********************************************************/

// Returns the name of a work completion opcode, for debug logging.
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

// Work-completion error class: FATAL (local programming error), TRANSIENT (transport/peer, may
// clear on reconnect), FLUSH (WR flushed as a consequence of an earlier error).
typedef enum {
  NAAICE_WC_FATAL = 0,
  NAAICE_WC_TRANSIENT = 1,
  NAAICE_WC_FLUSH = 2,
} naaice_wc_class;

naaice_wc_class naaice_classify_wc_status(enum ibv_wc_status status) {
  switch (status) {
    case IBV_WC_WR_FLUSH_ERR:
      return NAAICE_WC_FLUSH;
    case IBV_WC_RETRY_EXC_ERR:
    case IBV_WC_RNR_RETRY_EXC_ERR:
    case IBV_WC_REM_ACCESS_ERR:
    case IBV_WC_REM_OP_ERR:
    case IBV_WC_REM_INV_REQ_ERR:
    case IBV_WC_REM_ABORT_ERR:
    case IBV_WC_RESP_TIMEOUT_ERR:
      return NAAICE_WC_TRANSIENT;
    default:
      return NAAICE_WC_FATAL;
  }
}

const char *naaice_wc_class_str(naaice_wc_class wc_class) {
  switch (wc_class) {
    case NAAICE_WC_FLUSH:
      return "FLUSH-CASCADE";
    case NAAICE_WC_TRANSIENT:
      return "TRANSIENT";
    default:
      return "FATAL";
  }
}

// Returns the name of a naaice connection state, for debug logging.
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

#ifndef NAAICE_NO_REGISTER_ALARM_HANDLER
void alarm_handler(__attribute__((unused)) int signo) {
  ulog_warn("Timeout reached. Signal: %d\n", signo);
}
#endif

// Generates dummy NAA addresses laid out sequentially from offset by region
// size. To be replaced by a request to the memory management service.
void get_sequential_naa_addresses(unsigned int n_mrs, uint64_t offset, size_t *mr_sizes,
                                  uint64_t *sequential_addrs) {
  uint64_t curr_addr = offset;
  for (unsigned int i = 0; i < n_mrs; i++) {
    sequential_addrs[i] = curr_addr;
    curr_addr += ((mr_sizes[i] + NAA_PAGE_WIDTH - 1) / NAA_PAGE_WIDTH) *
                 NAA_PAGE_WIDTH;  // aligned to NAA page width
  }
}

#ifdef REMOTE_MEMORY_MANAGEMENT_SERVICE
int get_rmms_naa_addresses(unsigned int n_mrs, size_t *mr_sizes, uint64_t *sequential_addrs,
                           struct naaice_rmms_data *rmms_data) {
  curl_global_init(CURL_GLOBAL_DEFAULT);
  int user_id;

  unsigned int *alloc_size = calloc(n_mrs, sizeof(unsigned int));

  ulog_debug("%d\n", n_mrs);
  for (unsigned int i = 0; i < n_mrs; i++) {
    alloc_size[i] = (unsigned int)mr_sizes[i];
    ulog_debug("Size %d: %d\n", i, alloc_size[i]);
  }

  user_id = rmms_type_alloc(alloc_size, sequential_addrs, n_mrs, rmms_data->rmms_server_address,
                            rmms_data->rmms_port, rmms_data->fpga_mnemonic, rmms_data->mem_type);

  if (user_id < 0) {
    ulog_error("Error while contacting the memory server\n");
    return -1;
  }
  ulog_debug("received id: %d and reserved %d addresses\n", user_id, n_mrs);
  for (unsigned int i = 0; i < n_mrs; i++) {
    ulog_debug("Addr %d: %ld\n", i, sequential_addrs[i]);
  }

  free(alloc_size);

  curl_global_cleanup();

  return user_id;
}
#endif

/* Function Implementations **************************************************/

int naaice_init_communication_context_with_addresses(
    struct naaice_communication_context **comm_ctx, uint64_t *naa_addresses, char **params,
    unsigned int params_amount, size_t *mr_sizes, unsigned int internal_mr_amount, uint8_t fncode,
    const char *local_address, const char *remote_address, uint16_t remote_cm_port) {
  ulog_trace("In naaice_init_communication_context\n");

  // Function code must be positive.
  if (fncode < 1) {
    ulog_error("Function code should be positive.\n");
    return -1;
  }

  // Allocate the communication context.
  *comm_ctx =
      (struct naaice_communication_context *)malloc(sizeof(struct naaice_communication_context));
  if (*comm_ctx == NULL) {
    ulog_error("Failed to allocate memory for communication context.\n");
    return -1;
  }

  // Create the RDMA event channel.
  (*comm_ctx)->ev_channel = rdma_create_event_channel();
  if (!(*comm_ctx)->ev_channel) {
    ulog_error("Failed to create an RDMA event channel.\n");
    return -1;
  }

  // Create the RDMA communication ID.
  struct rdma_cm_id *rdma_comm_id;
  if (rdma_create_id((*comm_ctx)->ev_channel, &rdma_comm_id, NULL, RDMA_PS_TCP) == -1) {
    ulog_error("Failed to create an RDMA communication id.\n");
    return -1;
  }

  // Mark egress ECN-Capable so a congested switch marks instead of dropping. Best-effort.
  uint8_t roce_tos = NAAICE_ROCE_TOS;
  if (rdma_set_option(rdma_comm_id, RDMA_OPTION_ID, RDMA_OPTION_ID_TOS, &roce_tos,
                      sizeof(roce_tos))) {
    ulog_warn("rdma_set_option(TOS=0x%02x) failed; egress may not be ECN-capable.", roce_tos);
  }

  // Initialize the communication context fields.
  (*comm_ctx)->state = NAAICE_INIT;
  (*comm_ctx)->id = rdma_comm_id;
  (*comm_ctx)->no_local_mrs = params_amount;  // Symmetric regions, so local
  (*comm_ctx)->no_peer_mrs = params_amount;   // and remote counts match.
  (*comm_ctx)->no_internal_mrs = 0;
  (*comm_ctx)->mr_return_idx = 0;
  (*comm_ctx)->rdma_writes_done = 0;
  (*comm_ctx)->response_received = false;
  (*comm_ctx)->fncode = fncode;
  (*comm_ctx)->no_input_mrs = 0;
  (*comm_ctx)->no_output_mrs = 0;
  (*comm_ctx)->immediate = 0;
  (*comm_ctx)->bytes_received = 0;
  (*comm_ctx)->response_user_immediate = 0;
  (*comm_ctx)->no_rpc_calls = 0;
  (*comm_ctx)->timeout = DEFAULT_TIMEOUT;
  (*comm_ctx)->retry_count = DEFAULT_RETRY_COUNT;
#ifdef REMOTE_MEMORY_MANAGEMENT_SERVICE
  // NULL when RMMS is compiled in but not used.
  (*comm_ctx)->rmms_data = NULL;
#endif

  // Set up internal memory regions, those used on the NAA for calculation.
  if (naaice_set_internal_mrs(*comm_ctx, internal_mr_amount, naa_addresses, mr_sizes)) {
    return -1;
  }

  // Set up the memory regions corresponding to parameters.
  if (naaice_set_parameter_mrs(*comm_ctx, params_amount, (uint64_t *)params,
                               &naa_addresses[internal_mr_amount], &mr_sizes[internal_mr_amount])) {
    return -1;
  }

  // Set the immediate value sent later with the data transfer.
  uint8_t *imm_bytes = (uint8_t *)calloc(3, sizeof(uint8_t));
  if (naaice_set_immediate(*comm_ctx, imm_bytes)) {
    return -1;
  }

  // Region for building MRSP messages. Fixed size of an advertisement +
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
  snprintf(port_str, sizeof(port_str) / sizeof(port_str[0]), "%u", remote_cm_port);

  // Resolve the remote address.
  struct addrinfo *rem_addr = NULL;
  if (getaddrinfo(remote_address, port_str, NULL, &rem_addr)) {
    ulog_error("Failed to get address info for remote address.\n");
    return -1;
  }

  int resolve_addr_result = 0;

  // Resolve the local address if one was provided.
  if (local_address != NULL && strlen(local_address) > 0) {
    struct addrinfo *loc_addr = NULL;
    if (getaddrinfo(local_address, NULL, NULL, &loc_addr)) {
      ulog_error("Failed to get address info for local address.\n");
      return -1;
    }
    ulog_debug("Local IP provided.\n");
    resolve_addr_result =
        rdma_resolve_addr(rdma_comm_id, loc_addr->ai_addr, rem_addr->ai_addr, TIMEOUT_RESOLVE_ADDR);

    freeaddrinfo(loc_addr);
  } else {
    ulog_debug("No local IP provided.\n");
    resolve_addr_result =
        rdma_resolve_addr(rdma_comm_id, NULL, rem_addr->ai_addr, TIMEOUT_RESOLVE_ADDR);
  }

  if (resolve_addr_result == -1) {
    ulog_error("Failed to resolve addresses (errno %d).\n", errno);
    return -1;
  }

  // Free the resolved remote address.
  freeaddrinfo(rem_addr);
  return 0;
}

#ifdef REMOTE_MEMORY_MANAGEMENT_SERVICE
int naaice_init_rmms_data(struct naaice_rmms_data **rmms_data, const char *remote_malloc_address,
                          uint16_t remote_malloc_port, const char *fpga_key, const char *mem_type) {
  *rmms_data = calloc(1, sizeof(struct naaice_rmms_data));
  if (*rmms_data == NULL) return -1;

  // Copy the RMMS server address.
  size_t rmms_addr_len = strlen(remote_malloc_address);
  if (rmms_addr_len > MAX_RMMS_ADDRESS_LEN) {
    ulog_error("RMMS adress was to long.\n");
    return -1;
  }

  (*rmms_data)->rmms_server_address = strndup(remote_malloc_address, rmms_addr_len);
  if ((*rmms_data)->rmms_server_address == NULL) {
    ulog_error("Unable to allocate RMMS address space.\n");
    return -1;
  }

  (*rmms_data)->rmms_port = remote_malloc_port;

  // Copy the FPGA mnemonic.
  size_t rmms_mnemonic_len = strlen(fpga_key);
  if (rmms_mnemonic_len > MAX_FPGA_MNEMONIC_LEN) {
    ulog_error("RMMS FPGA mnemonic was to long.\n");
    return -1;
  }

  (*rmms_data)->fpga_mnemonic = strndup(fpga_key, rmms_mnemonic_len);
  if ((*rmms_data)->rmms_server_address == NULL) {
    ulog_error("Unable to allocate RMMS FPGA mnemonic space.\n");
    return -1;
  }

  // Copy the memory type, if one was specified.
  if (mem_type == NULL) {
    (*rmms_data)->mem_type = NULL;
    return 0;
  }

  size_t rmms_mem_type_len = strlen(mem_type);
  if (rmms_mem_type_len > MAX_RMMS_MEM_TYPE_LEN) {
    ulog_error("RMMS memory type was too long.\n");
    return -1;
  }

  (*rmms_data)->mem_type = strndup(mem_type, rmms_mem_type_len);
  if ((*rmms_data)->mem_type == NULL) {
    ulog_error("Unable to allocate memory type space.\n");
    return -1;
  }

  return 0;
}

int naaice_init_communication_context_with_rmms(
    struct naaice_communication_context **comm_ctx, size_t *param_sizes, char **params,
    unsigned int params_amount, unsigned int internal_mr_amount, size_t *internal_mr_sizes,
    uint8_t fncode, const char *local_address, const char *remote_address, uint16_t remote_cm_port,
    struct naaice_rmms_data *rmms_data) {
  uint64_t *naa_addresses = calloc(internal_mr_amount + params_amount, sizeof(uint64_t));
  if (naa_addresses == NULL) return -1;

  size_t *mr_sizes = calloc(internal_mr_amount + params_amount, sizeof(size_t));
  if (mr_sizes == NULL) {
    free(naa_addresses);
    return -1;
  }

  for (unsigned int i = 0; i < internal_mr_amount; i++) {
    mr_sizes[i] = internal_mr_sizes[i];
  }
  for (unsigned int i = 0; i < params_amount; i++) {
    mr_sizes[i + internal_mr_amount] = param_sizes[i];
  }

  rmms_data->rmms_id = get_rmms_naa_addresses(internal_mr_amount + params_amount, mr_sizes,
                                              naa_addresses, rmms_data);

  if (rmms_data->rmms_id < 0) {
    ulog_error("No valid RMMS id.\n");
    return -1;
  }

  int ret_val = naaice_init_communication_context_with_addresses(
      comm_ctx, naa_addresses, params, params_amount, mr_sizes, internal_mr_amount, fncode,
      local_address, remote_address, remote_cm_port);

  free(mr_sizes);
  free(naa_addresses);
  return ret_val;
}
#endif

int naaice_init_communication_context(struct naaice_communication_context **comm_ctx,
                                      uint64_t addr_offset, size_t *param_sizes, char **params,
                                      unsigned int params_amount, unsigned int internal_mr_amount,
                                      size_t *internal_mr_sizes, uint8_t fncode,
                                      const char *local_address, const char *remote_address,
                                      uint16_t port) {
  // Sequential NAA memory addresses starting at addr_offset.
  uint64_t naa_addresses[internal_mr_amount + params_amount];
  size_t mr_sizes[internal_mr_amount + params_amount];
  for (unsigned int i = 0; i < internal_mr_amount; i++) {
    mr_sizes[i] = internal_mr_sizes[i];
  }
  for (unsigned int i = 0; i < params_amount; i++) {
    mr_sizes[i + internal_mr_amount] = param_sizes[i];
  }
  get_sequential_naa_addresses(internal_mr_amount + params_amount, addr_offset, mr_sizes,
                               naa_addresses);

  return naaice_init_communication_context_with_addresses(
      comm_ctx, naa_addresses, params, params_amount, mr_sizes, internal_mr_amount, fncode,
      local_address, remote_address, port);
}

int naaice_poll_connection_event(struct naaice_communication_context *comm_ctx,
                                 struct rdma_cm_event *ev, struct rdma_cm_event *ev_cp) {
  ulog_trace("In naaice_poll_connection_event\n");

  // Poll the event channel fd so the wait is bounded, not indefinite.
  struct pollfd poll_fd;
  poll_fd.fd = comm_ctx->ev_channel->fd;
  poll_fd.events = POLLIN;
  poll_fd.revents = 0;

  int poll_result = poll(&poll_fd, 1, POLLING_TIMEOUT);
  if (poll_result <= 0) {
    return -1;
  }

  if (!rdma_get_cm_event(comm_ctx->ev_channel, &ev)) {
    // Acking frees the event, so copy it first and use the copy afterwards.
    memcpy(ev_cp, ev, sizeof(*ev));

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
    // Skip if this event was already handled.
    if (comm_ctx->state != NAAICE_READY) {
      comm_ctx->state = NAAICE_READY;

      // This needs to happen before the route resolution event.
      // Enforced this using the state machine.
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
    // Ensure address resolution has completed (state becomes NAAICE_READY).
    if (comm_ctx->state != NAAICE_READY) {
      struct rdma_cm_event ev;
      ev.event = RDMA_CM_EVENT_ADDR_RESOLVED;
      naaice_handle_addr_resolved(comm_ctx, &ev);
    }

    // Connection parameters.
    struct rdma_conn_param cm_params;
    memset(&cm_params, 0, sizeof(cm_params));
    cm_params.retry_count = comm_ctx->retry_count;
    cm_params.initiator_depth = 1;
    cm_params.responder_resources = 1;
    cm_params.rnr_retry_count = 6;  // 7 would be indefinite

    uint8_t ack_timeout = DEFAULT_ACK_TIMEOUT;
    if (rdma_set_option(comm_ctx->id, RDMA_OPTION_ID, RDMA_OPTION_ID_ACK_TIMEOUT, &ack_timeout,
                        sizeof(ack_timeout))) {
      ulog_error("Failed to set the ack timeout.\n");
    }

    if (rdma_connect(comm_ctx->id, &cm_params)) {
      ulog_error("RDMA connection failed.\n");
      return -1;
    }
  }

  return 0;
}

int naaice_handle_connection_established(struct naaice_communication_context *comm_ctx,
                                         struct rdma_cm_event *ev) {
  ulog_trace("In naaice_handle_connection_established\n");

  if (ev->event == RDMA_CM_EVENT_ESTABLISHED) {
    comm_ctx->state = NAAICE_CONNECTED;

    // A retry budget past POLLING_TIMEOUT turns one lost packet into a failed transfer.
    struct ibv_qp_attr qa;
    struct ibv_qp_init_attr qia;
    if (comm_ctx->qp != NULL &&
        ibv_query_qp(comm_ctx->qp, &qa, IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT, &qia) == 0 &&
        qa.timeout != DEFAULT_ACK_TIMEOUT) {
      ulog_error(
          "QP 0x%x ack timeout is %u, not %u: one lost packet takes %.0f ms to recover, "
          "against POLLING_TIMEOUT %d ms.",
          comm_ctx->qp->qp_num, qa.timeout, DEFAULT_ACK_TIMEOUT,
          4.096e-3 * (double)(1u << qa.timeout) * (double)qa.retry_cnt, POLLING_TIMEOUT);
    }
  }
  return 0;
}

int naaice_handle_error(struct naaice_communication_context *comm_ctx, struct rdma_cm_event *ev) {
  ulog_trace("In naaice_handle_error\n");

  // For each known error event, set the error state (so the upstream
  // naaice_setup_connection loop can exit), log it, and return -1.
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
    // Is a disconnect here an error? Review what to clean up in each state.
    // We don't expect a disconnect at this point, so exit.
    ulog_error("RDMA disconnected by server.\n");
    // Keep the current state so cleanup matches what was allocated.
    naaice_disconnect_and_cleanup(comm_ctx);
    return -1;
  }

  return 0;
}

int naaice_handle_other(__attribute__((unused)) struct naaice_communication_context *comm_ctx,
                        __attribute__((unused)) struct rdma_cm_event *ev) {
  ulog_trace("In naaice_handle_other\n");
  ulog_error("Unknown event: %d.\n", ev->event);
  return -1;
}

int naaice_poll_and_handle_connection_event(struct naaice_communication_context *comm_ctx) {
  ulog_trace("In naaice_poll_and_handle_connection_event\n");

  // Dispatch a received event to its handler.
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
      // TODO(#3): disconnect probably shouldn't be treated as an error event.
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
  // Success whether an event was handled or none was received.
  return 0;
}

int naaice_setup_connection(struct naaice_communication_context *comm_ctx) {
  ulog_trace("In naaice_setup_connection\n");

  // Handle events until the connection is established.
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

  // id->verbs is only set after rdma_resolve_addr or rdma_resolve_route.
  comm_ctx->ibv_ctx = comm_ctx->id->verbs;

  // Protection domain.
  ulog_debug("Making protection domain.\n");
  comm_ctx->pd = ibv_alloc_pd(comm_ctx->ibv_ctx);
  if (comm_ctx->pd == NULL) {
    ulog_error("Failed to create an RDMA protection domain.\n");
    return -1;
  }

  // Completion channel.
  ulog_debug("Making completion channel.\n");
  comm_ctx->comp_channel = ibv_create_comp_channel(comm_ctx->ibv_ctx);
  if (comm_ctx->comp_channel == NULL) {
    ulog_error("Failed to create an IBV completion channel.\n");
    return -1;
  }

  // Completion queue.
  ulog_debug("Making completion queue.\n");
  comm_ctx->cq = ibv_create_cq(comm_ctx->ibv_ctx, RX_DEPTH + 1, NULL, comm_ctx->comp_channel, 0);
  if (comm_ctx->cq == NULL) {
    ulog_error("Failed to create an IBV completion queue.\n");
    return -1;
  }

  // Request completion queue notifications.
  ulog_debug("Requesting completion queue notifications.\n");
  if (ibv_req_notify_cq(comm_ctx->cq, 1)) {
    ulog_error(
        "Failed to request completion channel notifications "
        "on completion queue.\n");
    return -1;
  }

  uint32_t inline_sizes[] = {512, 256, 128, 64, 0};
  int num_sizes = sizeof(inline_sizes) / sizeof(inline_sizes[0]);
  bool qp_created = false;
  // Queue pair attributes.
  struct ibv_qp_init_attr init_attr = {
      .send_cq = comm_ctx->cq,
      .recv_cq = comm_ctx->cq,
      // Exceeding max WRs causes ENOMEM in ibv_post_send().
      // TODO(#7): pick a lower number when not measuring transfer performance.
      .cap = {.max_send_wr = RX_DEPTH,
              .max_recv_wr = RX_DEPTH,
              .max_send_sge = 32,
              .max_recv_sge = 32,
              .max_inline_data = inline_sizes[0]},
      .qp_type = IBV_QPT_RC,
      .sq_sig_all = 1};

  // Try decreasing inline sizes until QP creation succeeds.
  for (int i = 0; i < num_sizes && !qp_created; i++) {
    init_attr.cap.max_inline_data = inline_sizes[i];
    struct ibv_device_attr device_attr;
    if (ibv_query_device(comm_ctx->ibv_ctx, &device_attr)) {
      ulog_error("Failed to query device\n");
    }

    // Queue pair.
    ulog_debug("Making queue pair.\n");
    if (!rdma_create_qp(comm_ctx->id, comm_ctx->pd, &init_attr)) {
      qp_created = true;
      ulog_info("QP created with max_inline_data=%u (actual=%u)\n", inline_sizes[i],
                init_attr.cap.max_inline_data);
    } else {
      ulog_debug("QP creation failed with max_inline_data=%u, trying smaller...\n",
                 inline_sizes[i]);
    }
  }

  if (!qp_created) {
    perror("Failed to create an RDMA queue pair with any inline data size (even 0)\n");
    return -1;
  }
  comm_ctx->qp = comm_ctx->id->qp;
  comm_ctx->max_inline_data = init_attr.cap.max_inline_data;
  return 0;
}

int naaice_register_mrs(struct naaice_communication_context *comm_ctx) {
  ulog_trace("In naaice_register_mrs\n");

  // Register all parameter regions and the metadata region.
  for (int i = 0; i < ((int)comm_ctx->no_local_mrs); i++) {
    comm_ctx->mr_local_data[i].ibv =
        ibv_reg_mr(comm_ctx->pd, comm_ctx->mr_local_data[i].addr, comm_ctx->mr_local_data[i].size,
                   (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));

    if (comm_ctx->mr_local_data[i].ibv == NULL) {
      ulog_error("client: Failed to register memory for local memory region.\n");
      return -1;
    }
  }

  // Register the MRSP message region.
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
                             unsigned int n_parameter_mrs, uint64_t *local_addrs,
                             uint64_t *remote_addrs, size_t *sizes) {
  ulog_trace("In naaice_set_parameter_mrs\n");

  // Local and peer (remote) region counts. Regions are symmetric so the
  // counts match; internal regions are not counted here.
  comm_ctx->no_local_mrs = n_parameter_mrs;
  comm_ctx->no_peer_mrs = n_parameter_mrs;

  // Local memory region info structs.
  comm_ctx->mr_local_data =
      (struct naaice_mr_local *)calloc(comm_ctx->no_local_mrs, sizeof(struct naaice_mr_local));
  if (!comm_ctx->mr_local_data) {
    ulog_error("Failed to allocate memory for local mr info structures.\n");
    return -1;
  }

  for (unsigned int i = 0; i < comm_ctx->no_local_mrs; i++) {
    comm_ctx->mr_local_data[i].size = sizes[i];
    comm_ctx->mr_local_data[i].addr = (char *)local_addrs[i];

    // to_write marks a local MR as an input to be written to the NAA. Set
    // before the first RPC via naaice_set_input_mr.
    comm_ctx->mr_local_data[i].to_write = false;

    // single_send marks an MR to be sent only on the first RPC of this
    // connection. Set before the first RPC via naaice_set_singlesend_mr.
    comm_ctx->mr_local_data[i].single_send = false;
  }

  // Peer (remote) memory region info structs.
  comm_ctx->mr_peer_data =
      (struct naaice_mr_peer *)calloc(comm_ctx->no_peer_mrs, sizeof(struct naaice_mr_peer));
  if (!comm_ctx->mr_peer_data) {
    ulog_error("Failed to allocate memory for remote mr info structures.\n");
    return -1;
  }

  for (unsigned int i = 0; i < comm_ctx->no_peer_mrs; i++) {
    comm_ctx->mr_peer_data[i].size = sizes[i];

    // Remote addresses (requested on the NAA) must fit within 7 bytes.
    comm_ctx->mr_peer_data[i].addr = remote_addrs[i];

    // to_write marks a remote MR as an output to be written back from the
    // NAA. Set before the first RPC via naaice_set_output_mr.
    comm_ctx->mr_peer_data[i].to_write = false;

    // single_send marks an MR expected only on the first RPC of this
    // connection. Set before the first RPC via naaice_set_singlesend_mr.
    comm_ctx->mr_peer_data[i].single_send = false;

    // TODO(#7): decide whether alignment belongs to AP1 or the user (optional, improves
    // performance).
  }

  return 0;
}

int naaice_set_input_mr(struct naaice_communication_context *comm_ctx, unsigned int input_mr_idx) {
  ulog_trace("In naaice_set_input_mr\n");

  // Validate the parameter region index.
  if (input_mr_idx >= comm_ctx->no_local_mrs) {
    ulog_error(
        "Tried to set invalid memory region #%d as input, there "
        "are only %d local memory regions.\n",
        input_mr_idx, comm_ctx->no_local_mrs);
    return -1;
  }

  // Mark as input and count it, unless already marked.
  if (!comm_ctx->mr_local_data[input_mr_idx].to_write) {
    comm_ctx->mr_local_data[input_mr_idx].to_write = true;
    comm_ctx->no_input_mrs++;
  }

  return 0;
}

int naaice_set_output_mr(struct naaice_communication_context *comm_ctx,
                         unsigned int output_mr_idx) {
  ulog_trace("In naaice_set_output_mr\n");

  // Validate the parameter region index.
  if (output_mr_idx >= comm_ctx->no_peer_mrs) {
    ulog_error(
        "Tried to set invalid memory region #%d as output, there "
        "are only %d local memory regions.\n",
        output_mr_idx, comm_ctx->no_peer_mrs);
    return -1;
  }

  // Mark as output and count it, unless already marked.
  if (!comm_ctx->mr_peer_data[output_mr_idx].to_write) {
    comm_ctx->mr_peer_data[output_mr_idx].to_write = true;
    comm_ctx->no_output_mrs++;
  }

  return 0;
}

int naaice_set_singlesend_mr(struct naaice_communication_context *comm_ctx,
                             unsigned int singlesend_mr_idx) {
  ulog_trace("In naaice_set_singlesend_mr\n");

  // Validate the parameter region index.
  if (singlesend_mr_idx >= comm_ctx->no_peer_mrs) {
    ulog_error(
        "Tried to set invalid memory region #%d as single send, "
        "there are only %d local memory regions.\n",
        singlesend_mr_idx, comm_ctx->no_peer_mrs);
    return -1;
  }

  comm_ctx->mr_local_data[singlesend_mr_idx].single_send = true;

  // A single-send MR is also an input; mark and count it unless already done.
  if (!comm_ctx->mr_local_data[singlesend_mr_idx].to_write) {
    comm_ctx->mr_local_data[singlesend_mr_idx].to_write = true;
    comm_ctx->no_input_mrs++;
  }

  return 0;
}

int naaice_set_internal_mrs(struct naaice_communication_context *comm_ctx,
                            unsigned int n_internal_mrs, uint64_t *addrs, size_t *sizes) {
  ulog_trace("In naaice_set_internal_mrs\n");

  comm_ctx->no_internal_mrs = n_internal_mrs;

  // Internal memory region info structs.
  comm_ctx->mr_internal = (struct naaice_mr_internal *)calloc(comm_ctx->no_internal_mrs,
                                                              sizeof(struct naaice_mr_internal));

  if (!comm_ctx->mr_internal) {
    ulog_error("Failed to allocate internal memory region structures.\n");
    return -1;
  }

  // Addresses must fit in 7 bytes.
  for (unsigned int i = 0; i < n_internal_mrs; i++) {
    comm_ctx->mr_internal[i].addr = addrs[i];
    comm_ctx->mr_internal[i].size = sizes[i];
  }

  return 0;
}

int naaice_set_immediate(struct naaice_communication_context *comm_ctx, uint8_t *imm_bytes) {
  // imm_bytes holds up to 3 bytes, placed in the 3 high bytes of the immediate.
  for (unsigned int i = 1; i < 4; i++) {
    comm_ctx->immediate_bytearr[i] = imm_bytes[i - 1];
  }

  // The first byte is always the function code.
  comm_ctx->immediate_bytearr[0] = comm_ctx->fncode;

  return 0;
}

int naaice_init_mrsp(struct naaice_communication_context *comm_ctx) {
  ulog_trace("In naaice_init_mrsp\n");

  // TODO(#3): handle errors from these.
  // Post the receive before sending, otherwise the response could race it.
  naaice_post_recv_mrsp(comm_ctx);

  // Send the MRSP advertisement + request.
  naaice_send_message(comm_ctx, MSG_MR_AAR, 0);

  return 0;
}

int naaice_init_data_transfer(struct naaice_communication_context *comm_ctx) {
  if (naaice_post_recv_data(comm_ctx)) {
    // On error, send fncode 0 and then exit the connection.
    comm_ctx->fncode = 0;
  }

  if (naaice_write_data(comm_ctx, comm_ctx->fncode)) {
    // TODO(#3): signal a write failure to the server.
  };

  comm_ctx->no_rpc_calls++;

  return 0;
}

int naaice_handle_work_completion(struct ibv_wc *wc,
                                  struct naaice_communication_context *comm_ctx) {
  ulog_trace("In naaice_handle_work_completion");

  ulog_debug("state: %s, opcode: %s", get_state_str(comm_ctx->state),
             get_ibv_wc_opcode_str(wc->opcode));

  // Fail on a non-success work completion.
  if (wc->status != IBV_WC_SUCCESS) {
    // Log decoded status + class so the cause is visible in CI logs.
    ulog_error(
        "Status is not IBV_WC_SUCCESS. Status %s (%d) [%s], vendor_err=0x%x, opcode=%s, "
        "wr_id=%llu, qp_num=0x%x, state=%s.",
        ibv_wc_status_str(wc->status), wc->status,
        naaice_wc_class_str(naaice_classify_wc_status(wc->status)), wc->vendor_err,
        get_ibv_wc_opcode_str(wc->opcode), (unsigned long long)wc->wr_id, wc->qp_num,
        get_state_str(comm_ctx->state));
    return -1;
  }

  // Sending the MRSP packet.
  if (comm_ctx->state == NAAICE_MRSP_SENDING) {
    // Send completed.
    if (wc->opcode == IBV_WC_SEND) {
      comm_ctx->state = NAAICE_MRSP_RECEIVING;
      return 0;
    }
  }

  // Waiting for an MRSP response from the NAA.
  else if (comm_ctx->state == NAAICE_MRSP_RECEIVING) {
    // Received an MRSP message (write without immediate); handle it.
    if (wc->opcode == IBV_WC_RECV) {
      struct naaice_mr_hdr *msg = (struct naaice_mr_hdr *)comm_ctx->mr_local_message->addr;

      // Memory region announcement.
      if (msg->type == MSG_MR_A) {
        struct naaice_mr_dynamic_hdr *dyn =
            (struct naaice_mr_dynamic_hdr *)(comm_ctx->mr_local_message->addr +
                                             sizeof(struct naaice_mr_hdr));

        // Record the NAA's memory regions from the message.
        struct naaice_mr_advertisement *mr =
            (struct naaice_mr_advertisement *)(comm_ctx->mr_local_message->addr +
                                               sizeof(struct naaice_mr_hdr) +
                                               sizeof(struct naaice_mr_dynamic_hdr));

        for (int i = 0; i < (dyn->count); i++) {
          comm_ctx->mr_peer_data[i].addr = ntohll(mr->addr);
          comm_ctx->mr_peer_data[i].rkey = ntohl(mr->rkey);
          comm_ctx->mr_peer_data[i].size = ntohl(mr->size);
          ulog_debug("Peer MR %d: Addr: %lX, Size: %lu, rkey: %d", i + 1,
                     comm_ctx->mr_peer_data[i].addr, comm_ctx->mr_peer_data[i].size,
                     comm_ctx->mr_peer_data[i].rkey);
          mr = (struct naaice_mr_advertisement *)(comm_ctx->mr_local_message->addr +
                                                  (sizeof(struct naaice_mr_hdr) +
                                                   sizeof(struct naaice_mr_dynamic_hdr) +
                                                   (i + 1) *
                                                       sizeof(struct naaice_mr_advertisement)));
        }

        comm_ctx->state = NAAICE_MRSP_DONE;

        return 0;
      }

      // Remote error reported.
      else if (msg->type == MSG_MR_ERR) {
        __attribute__((unused)) struct naaice_mr_error *err =
            (struct naaice_mr_error *)(comm_ctx->mr_local_message->addr +
                                       sizeof(struct naaice_mr_hdr));
        ulog_error(
            "Remote node encountered error in memory region exchange: "
            "%x.",
            err->code);
        return -1;
      }

      // Unknown message type.
      else {
        ulog_error("Unhandled MRSP packet type received: %d", msg->type);
        return -1;
      }
    }
  }

  // The last write's send completion and the response are not ordered against each other, so the
  // queue can hold them either way round. Both are recorded; the transfer ends when both hold.
  else if (comm_ctx->state == NAAICE_DATA_SENDING || comm_ctx->state == NAAICE_DATA_RECEIVING) {
    const bool handled = wc->opcode == IBV_WC_RDMA_WRITE ||
                         wc->opcode == IBV_WC_RECV ||  // no immediate, nothing to record
                         wc->opcode == IBV_WC_RECV_RDMA_WITH_IMM;

    if (handled) {
      if (wc->opcode == IBV_WC_RDMA_WRITE) {
        comm_ctx->rdma_writes_done++;
      } else if (wc->opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
        uint32_t user_immediate = ntohl(wc->imm_data) >> IMMEDIATE_OFFSET;
        if (user_immediate) {
          ulog_warn("Immediate return code non-zero: %d.", user_immediate);
        }

        comm_ctx->response_user_immediate = user_immediate;
        comm_ctx->bytes_received = wc->byte_len;
        comm_ctx->response_received = true;
      }

      // no_input_mrs is mutated elsewhere, so the tally is compared inclusively.
      if (comm_ctx->rdma_writes_done >= comm_ctx->no_input_mrs) {
        if (comm_ctx->response_received) {
          comm_ctx->rdma_writes_done = 0;
          comm_ctx->response_received = false;
          comm_ctx->state = NAAICE_FINISHED;
        } else {
          // Writes retired, response outstanding
          comm_ctx->state = NAAICE_DATA_RECEIVING;
        }
      }
      return 0;
    }
  }

  ulog_error("Work completion opcode (wc opcode): %s, not handled for state: %s.",
             get_ibv_wc_opcode_str(wc->opcode), get_state_str(comm_ctx->state));
  return -1;
}

// Handles every work completion the queue holds. Returns -1 on the first that fails.
static int naaice_drain_cq(struct naaice_communication_context *comm_ctx) {
  struct ibv_wc wc;

  for (;;) {
    int n_wcs = ibv_poll_cq(comm_ctx->cq, 1, &wc);
    if (n_wcs < 0) {
      ulog_error("ibv_poll_cq() failed.\n");
      return -1;
    }
    if (n_wcs == 0) {
      return 0;
    }
    if (naaice_handle_work_completion(&wc, comm_ctx)) {
      ulog_error("Error while handling work completion.\n");
      return -1;
    }
  }
}

int naaice_poll_cq_blocking(struct naaice_communication_context *comm_ctx) {
  ulog_trace("In naaice_poll_cq_blocking");

#ifndef NAAICE_NO_REGISTER_ALARM_HANDLER
  // Signal handler to end the blocking call after the timeout.
  struct sigaction sa;
  sa.sa_handler = alarm_handler;
  sa.sa_flags = 0;  // SA_RESTART is not set
  sigemptyset(&sa.sa_mask);
  sigaction(SIGALRM, &sa, NULL);
  alarm((int)comm_ctx->timeout);
#endif

  struct ibv_cq *ev_cq;
  void *ev_ctx;

  // Ensure completion channel is in blocking mode.
  int fd_flags = fcntl(comm_ctx->comp_channel->fd, F_GETFL);
  if (fcntl(comm_ctx->comp_channel->fd, F_SETFL, fd_flags & ~O_NONBLOCK) < 0) {
    ulog_error(
        "Failed to change file descriptor of completion event "
        "channel.\n");
    return -1;
  }

  // Get the completion event.
  if (ibv_get_cq_event(comm_ctx->comp_channel, &ev_cq, &ev_ctx)) {
    ulog_error("Failed to get completion queue event.\n");
    if (naaice_disconnect_and_cleanup(comm_ctx)) {
      ulog_error("Error in cleanup procedure.\n");
    };
    return -1;
  }

  ibv_ack_cq_events(ev_cq, 1);

  // Armed before the drain, so a completion arriving from here on raises an event.
  if (ibv_req_notify_cq(comm_ctx->cq, 1)) {
    ulog_error(
        "Failed to request completion channel notifications on completion "
        "queue.\n");
    return -1;
  }

  if (naaice_drain_cq(comm_ctx)) {
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
    ulog_error(
        "Failed to change file descriptor of completion event "
        "channel.\n");
    return -1;
  }

  struct pollfd my_pollfd;
  my_pollfd.fd = comm_ctx->comp_channel->fd;
  my_pollfd.events = POLLIN;
  my_pollfd.revents = 0;

  // Nonblocking: return on timeout.
  int poll_result = poll(&my_pollfd, 1, POLLING_TIMEOUT);
  if (poll_result < 0) {
    ulog_error("Error occurred when polling completion channel.\n");
    return -1;
  } else if (poll_result == 0) {
    // No events received.
    return 0;
  }

  // Get the completion event.
  if (ibv_get_cq_event(comm_ctx->comp_channel, &ev_cq, &ev_ctx)) {
    ulog_error("Failed to get completion queue event.\n");
    return -1;
  }

  ibv_ack_cq_events(ev_cq, 1);

  // Armed before the drain, as in the blocking poll.
  // TODO(#7): use a solicited event to wake only on incoming RDMA-with-immediate.
  if (ibv_req_notify_cq(comm_ctx->cq, 1)) {
    ulog_error(

        "Failed to request completion channel notifications on completion "
        "queue.\n");
    return -1;
  }

  if (naaice_drain_cq(comm_ctx)) {
    return -1;
  }

  return 0;
}

int naaice_poll_cq_busy(struct naaice_communication_context *comm_ctx) {
  struct timespec start, now;
  clock_gettime(CLOCK_MONOTONIC, &start);
  while (comm_ctx->state < NAAICE_FINISHED) {
    struct ibv_wc wc;
    int n = ibv_poll_cq(comm_ctx->cq, 1, &wc);

    if (n > 0) {
      if (naaice_handle_work_completion(&wc, comm_ctx)) {
        return -1;
      }
    } else if (n < 0) {
      ulog_error("ibv_poll_cq failed\n");
      return -1;
    }

    // Check timeout (POLLING_TIMEOUT in ms).
    if (POLLING_TIMEOUT > 0) {
      clock_gettime(CLOCK_MONOTONIC, &now);
      long elapsed_ms =
          (now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000;

      if (elapsed_ms > POLLING_TIMEOUT) {
        ulog_error("Timeout after %ld ms. state=%s, writes %u of %u, response %s, qp=0x%x.",
                   elapsed_ms, get_state_str(comm_ctx->state), comm_ctx->rdma_writes_done,
                   comm_ctx->no_input_mrs, comm_ctx->response_received ? "received" : "outstanding",
                   comm_ctx->qp != NULL ? comm_ctx->qp->qp_num : 0);
        return -1;
      }
    }
  }
  return 0;
}

int naaice_do_mrsp(struct naaice_communication_context *comm_ctx) {
  ulog_trace("In naaice_do_mrsp\n");

  if (naaice_init_mrsp(comm_ctx)) {
    return -1;
  }

  // Poll and handle work completions until the MRSP is done.
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

  if (naaice_init_data_transfer(comm_ctx)) {
    return -1;
  }

  // Poll and handle work completions until the transfer (send, calculation,
  // receive) is complete.
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

int naaice_disconnect_and_cleanup(struct naaice_communication_context *comm_ctx) {
  // TODO(#4): derive cleanup from the communication state.
  ulog_trace("In naaice_disconnect_and_cleanup\n");

  // NULL-safe, idempotent, best-effort teardown: guard each resource and NULL it after release so
  // a partial or double disconnect can't deref freed memory.
  if (comm_ctx == NULL) {
    return 0;
  }

  // Disconnect (id is NULL if connection setup never completed).
  if (comm_ctx->id != NULL) {
    rdma_disconnect(comm_ctx->id);
  }

  int err = 0;
  int rc = 0;

  // Deregister memory regions (NULL when setup aborted before MR registration).
  if (comm_ctx->mr_local_message != NULL && comm_ctx->mr_local_message->ibv != NULL) {
    err = ibv_dereg_mr(comm_ctx->mr_local_message->ibv);
    if (err) {
      ulog_error(
          "Deregistration of local message memory region failed with "
          "error %d.\n",
          err);
      rc = -1;
    }
  }
  if (comm_ctx->mr_local_data != NULL) {
    for (int i = 0; i < comm_ctx->no_local_mrs; i++) {
      if (comm_ctx->mr_local_data[i].ibv == NULL) {
        continue;
      }
      err = ibv_dereg_mr(comm_ctx->mr_local_data[i].ibv);
      if (err) {
        ulog_error(
            "Deregistration of local data memory region failed with "
            "error %d.\n",
            err);
        rc = -1;
      }
    }
  }

  if (comm_ctx->state >= NAAICE_MRSP_DONE && comm_ctx->mr_peer_data != NULL) {
    free(comm_ctx->mr_peer_data);
    comm_ctx->mr_peer_data = NULL;
  }

  if (comm_ctx->mr_local_message != NULL) {
    free((void *)(comm_ctx->mr_local_message->addr));
    free(comm_ctx->mr_local_message);
    comm_ctx->mr_local_message = NULL;
  }
  if (comm_ctx->mr_local_data != NULL) {
    free(comm_ctx->mr_local_data);
    comm_ctx->mr_local_data = NULL;
  }

  // Destroy queue pair.
  if (comm_ctx->qp != NULL) {
    err = ibv_destroy_qp(comm_ctx->qp);
    if (err) {
      ulog_error("Destroying queue pair failed with error %d.\n", err);
      rc = -1;
    }
    comm_ctx->qp = NULL;
  }

  // Destroy completion queue.
  if (comm_ctx->cq != NULL) {
    err = ibv_destroy_cq(comm_ctx->cq);
    if (err) {
      ulog_error(
          "Destroying completion queue failed with "
          "error %d.\n",
          err);
      rc = -1;
    }
    comm_ctx->cq = NULL;
  }

  // Destroy completion channel.
  if (comm_ctx->comp_channel != NULL) {
    err = ibv_destroy_comp_channel(comm_ctx->comp_channel);
    if (err) {
      ulog_error(
          "Destroying completion channel failed with "
          "error %d.\n",
          err);
      rc = -1;
    }
    comm_ctx->comp_channel = NULL;
  }

  // Destroy protection domain.
  if (comm_ctx->pd != NULL) {
    err = ibv_dealloc_pd(comm_ctx->pd);
    if (err) {
      ulog_error(
          "Destroying protection domain failed with "
          "error %d.\n",
          err);
      rc = -1;
    }
    comm_ctx->pd = NULL;
  }

  // Destroy RDMA communication id.
  if (comm_ctx->id != NULL) {
    if (rdma_destroy_id(comm_ctx->id)) {
      perror("Failed to destroy RDMA communication id.\n");
      rc = -1;
    }
    comm_ctx->id = NULL;
  }

  // Destroy RDMA event channel.
  if (comm_ctx->ev_channel != NULL) {
    rdma_destroy_event_channel(comm_ctx->ev_channel);
    comm_ctx->ev_channel = NULL;
  }

#ifdef REMOTE_MEMORY_MANAGEMENT_SERVICE
  // Release all RMMS memory allocated by this instance. Skip if RMMS unused.
  if (comm_ctx->rmms_data == NULL) {
    return rc;
  }

  if (rmms_free_all(comm_ctx->rmms_data->rmms_id, comm_ctx->rmms_data->rmms_server_address,
                    comm_ctx->rmms_data->rmms_port, comm_ctx->rmms_data->fpga_mnemonic)) {
    ulog_error("Failed to free rmms remote memory\n");
    return -1;
  }

  free(comm_ctx->rmms_data->rmms_server_address);
  free(comm_ctx->rmms_data->fpga_mnemonic);

  if (comm_ctx->rmms_data->mem_type != NULL) free(comm_ctx->rmms_data->mem_type);

  free(comm_ctx->rmms_data);
  comm_ctx->rmms_data = NULL;
#endif

  return rc;
}

int naaice_send_message(struct naaice_communication_context *comm_ctx, enum message_id message_type,
                        uint8_t errorcode) {
  ulog_trace("In naaice_send_message\n");

  comm_ctx->state = NAAICE_MRSP_SENDING;

  // Every message starts with a header, built in the dedicated region
  // allocated in naaice_init_communication_context.
  struct naaice_mr_hdr *msg = (struct naaice_mr_hdr *)comm_ctx->mr_local_message->addr;

  // Running message size as fields are added.
  int msg_size = 0;
  msg_size += sizeof(struct naaice_mr_hdr);

  msg->type = message_type;

  // MRSP messages: advertisement, or advertisement + request.

  // Advertisement packet.
  if (msg->type == MSG_MR_A) {
    // Dynamic header.
    struct naaice_mr_dynamic_hdr *dyn =
        (struct naaice_mr_dynamic_hdr *)(msg + sizeof(struct naaice_mr_hdr));
    msg_size += sizeof(struct naaice_mr_dynamic_hdr);
    dyn->count = comm_ctx->no_local_mrs;
    dyn->padding[0] = 0;
    dyn->padding[1] = 0;

    // Current position in the message being built.
    struct naaice_mr_advertisement *curr;

    for (int i = 0; i < comm_ctx->no_local_mrs; i++) {
      // Advance to this region's slot in the packet.
      curr = (struct naaice_mr_advertisement *)(msg + sizeof(struct naaice_mr_hdr) +
                                                sizeof(struct naaice_mr_dynamic_hdr) +
                                                i * sizeof(struct naaice_mr_advertisement));

      // Fields for this memory region.
      curr->addr = htonll((uintptr_t)comm_ctx->mr_local_data[i].addr);
      curr->size = htonl(comm_ctx->mr_local_data[i].ibv->length);
      curr->rkey = htonl(comm_ctx->mr_local_data[i].ibv->rkey);

      msg_size += sizeof(struct naaice_mr_advertisement);
    }
  }

  // Advertisement + request packet.
  if (msg->type == MSG_MR_AAR) {
    // Dynamic header.
    struct naaice_mr_dynamic_hdr *dyn =
        (struct naaice_mr_dynamic_hdr *)(msg + sizeof(struct naaice_mr_hdr));
    msg_size += sizeof(struct naaice_mr_dynamic_hdr);
    dyn->padding[0] = 0;
    dyn->padding[1] = 0;

    // Advertised regions: parameters, plus the metadata region (counted in
    // no_local_mrs), plus the internal regions used by the NAA.
    dyn->count = comm_ctx->no_local_mrs + comm_ctx->no_internal_mrs;

    // Current position in the message being built.
    struct naaice_mr_advertisement_request *curr;

    for (int i = 0; i < comm_ctx->no_local_mrs; i++) {
      // Advance to this region's slot in the packet.
      curr = (struct naaice_mr_advertisement_request
                  *)(msg + sizeof(struct naaice_mr_hdr) + sizeof(struct naaice_mr_dynamic_hdr) +
                     i * sizeof(struct naaice_mr_advertisement_request));

      curr->addr = htonll((uintptr_t)comm_ctx->mr_local_data[i].addr);
      curr->size = htonl(comm_ctx->mr_local_data[i].ibv->length);
      curr->rkey = htonl(comm_ctx->mr_local_data[i].ibv->rkey);
      curr->mr_info_bytearray[7] = 0;

      // Flag a single-send region.
      if (comm_ctx->mr_local_data[i].single_send) {
        curr->mr_info_bytearray[7] |= MRFLAG_SINGLESEND;
      }

      // Flag an input region.
      if (comm_ctx->mr_local_data[i].to_write) {
        curr->mr_info_bytearray[7] |= MRFLAG_INPUT;
      }

      // Flag an output region.
      if (comm_ctx->mr_peer_data[i].to_write) {
        curr->mr_info_bytearray[7] |= MRFLAG_OUTPUT;
      }

      // Peer addresses were checked to fit in 7 bytes in set_parameter_mrs.
      for (int j = 0; j < 7; j++) {
        curr->mr_info_bytearray[j] = comm_ctx->mr_peer_data[i].fpgaaddr[j];
      }
      curr->mr_info = htonll(curr->mr_info);

      ulog_debug(
          "Local MR %d: Addr: %lX, Size: %ld, rkey: %d, Requested NAA "
          "Addr: %lX\n",
          i + 1, (uintptr_t)comm_ctx->mr_local_data[i].addr, comm_ctx->mr_local_data[i].ibv->length,
          comm_ctx->mr_local_data[i].ibv->rkey, comm_ctx->mr_peer_data[i].addr);

      msg_size += sizeof(struct naaice_mr_advertisement_request);
    }

    for (int i = 0; i < comm_ctx->no_internal_mrs; i++) {
      // Advance to this region's slot in the packet.
      curr = (struct naaice_mr_advertisement_request
                  *)(msg + sizeof(struct naaice_mr_hdr) + sizeof(struct naaice_mr_dynamic_hdr) +
                     comm_ctx->no_local_mrs * sizeof(struct naaice_mr_advertisement_request) +
                     i * sizeof(struct naaice_mr_advertisement_request));

      // Internal regions exist only on the NAA, so local addr and rkey are 0.
      curr->addr = htonll(0);
      curr->size = htonl(comm_ctx->mr_internal[i].size);
      curr->rkey = htonl(0);
      curr->mr_info_bytearray[7] = MRFLAG_INTERNAL;

      // Peer addresses were checked to fit in 7 bytes in set_internal_mrs.
      for (int j = 0; j < 7; j++) {
        curr->mr_info_bytearray[j] = comm_ctx->mr_internal[i].fpgaaddr[j];
      }
      curr->mr_info = htonll(curr->mr_info);

      ulog_debug("Internal MR %d: Addr: %lX, Size: %d, Requested NAA Addr: %lX\n", i + 1,
                 (uintptr_t)comm_ctx->mr_internal[i].addr, (int)comm_ctx->mr_internal[i].size,
                 comm_ctx->mr_internal[i].addr);

      msg_size += sizeof(struct naaice_mr_advertisement_request);
    }
  }

  // Error message.
  if (message_type == MSG_MR_ERR) {
    // Error packet, no dynamic header.
    struct naaice_mr_error *err = (struct naaice_mr_error *)(msg + sizeof(struct naaice_mr_hdr));

    // TODO(#3): define the meaning of error codes; only one (non-zero) is used.
    err->code = errorcode;

    msg_size += sizeof(struct naaice_mr_error);
  }

  // Scatter/gather element.
  struct ibv_sge sge;
  sge.addr = (uintptr_t)comm_ctx->mr_local_message->addr;
  sge.length = msg_size;
  sge.lkey = comm_ctx->mr_local_message->ibv->lkey;

  // Send work request.
  struct ibv_send_wr wr, *bad_wr = NULL;
  memset(&wr, 0, sizeof(wr));
  wr.wr_id = msg->type;  //(uintptr_t)comm_ctx;
  wr.opcode = IBV_WR_SEND;
  wr.sg_list = &sge;
  wr.num_sge = 1;
  wr.send_flags = IBV_SEND_SOLICITED;

  // Post the send.
  int post_result = ibv_post_send(comm_ctx->qp, &wr, &bad_wr);
  if (post_result) {
    ulog_error("Posting send for MRSP failed with error %d.\n", post_result);
    return -1;
  }

  return 0;
}

int naaice_set_bytes_to_send(struct naaice_communication_context *comm_ctx, int mr_idx,
                             int number_bytes) {
  if (mr_idx > comm_ctx->no_local_mrs - 1) {
    ulog_error("Index of memory region is out if bounds!\n");
    return -1;
  }

  if (comm_ctx->mr_local_data[mr_idx].ibv == NULL) {
    ulog_error(
        "Memory regions are not yet registered. Please call "
        "function after naaice_register_mrs!\n");
    return -1;
  }

  // A negative count resets the size to the full memory region length.
  if (number_bytes < 0) {
    comm_ctx->mr_local_data[mr_idx].size = comm_ctx->mr_local_data[mr_idx].ibv->length;
    return 0;
  }

  if ((size_t)number_bytes > comm_ctx->mr_local_data[mr_idx].ibv->length) {
    ulog_error("Number of specified bytes larger than size of memory region!\n");
    return -1;
  }

  comm_ctx->mr_local_data[mr_idx].size = number_bytes;

  return 0;
}

int naaice_write_data(struct naaice_communication_context *comm_ctx, uint8_t fncode) {
  ulog_trace("In naaice_write_data\n");
  ulog_debug("fncode: %d\n", fncode);

  // cleanup in case the transfer did not reach NAAICE_FINISHED
  comm_ctx->rdma_writes_done = 0;
  comm_ctx->response_received = false;

  comm_ctx->state = NAAICE_DATA_SENDING;

  // Function code 0 signals a host-side error: a single-byte write with a
  // zero immediate value.
  if (fncode == 0) {
    // Scatter/gather element.
    struct ibv_sge sge;
    sge.addr = (uintptr_t)comm_ctx->mr_local_data[0].addr;
    sge.length = 1;
    sge.lkey = comm_ctx->mr_local_data[0].ibv->lkey;

    // Write work request.
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

    // Post the write.
    int post_result = ibv_post_send(comm_ctx->qp, &wr, &bad_wr);
    if (post_result) {
      ulog_error(
          "Posting send for data write "
          "(while sending error message) failed with error %d.\n",
          post_result);
      return post_result;
    }
  }

  // Otherwise write every region flagged to_write (an input), set via
  // naaice_set_input_mr.
  else {
    // After the first RPC, drop single-send regions from those to be sent.
    for (unsigned int i = 0; i < comm_ctx->no_local_mrs; i++) {
      if (comm_ctx->mr_local_data[i].single_send && comm_ctx->mr_local_data[i].to_write &&
          (comm_ctx->no_rpc_calls > 0)) {
        comm_ctx->mr_local_data[i].to_write = false;
        comm_ctx->no_input_mrs--;
      }
    }

    // One write request and one scatter/gather element per region to write.
    struct ibv_send_wr wr[comm_ctx->no_input_mrs], *bad_wr = NULL;
    struct ibv_sge sge[comm_ctx->no_input_mrs];

    // Build a write request and SGE for each region to send.
    uint8_t mr_idx = 0;
    for (int i = 0; (i < comm_ctx->no_local_mrs) && (mr_idx < comm_ctx->no_input_mrs); i++) {
      if (comm_ctx->mr_local_data[i].to_write) {
        memset(&wr[mr_idx], 0, sizeof(wr[mr_idx]));
        wr[mr_idx].wr_id = mr_idx + 1;
        wr[mr_idx].sg_list = &sge[mr_idx];
        wr[mr_idx].num_sge = 1;
        wr[mr_idx].wr.rdma.remote_addr = comm_ctx->mr_peer_data[i].addr;
        wr[mr_idx].wr.rdma.rkey = comm_ctx->mr_peer_data[i].rkey;

        // Last region: write with immediate (function code + 24 bits from
        // naaice_set_immediate, 8th bit marks host-to-NAA). Others: plain write.
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
        if (sge[mr_idx].length <= comm_ctx->max_inline_data) {
          wr[mr_idx].send_flags |= IBV_SEND_INLINE;
        }
        sge[mr_idx].lkey = comm_ctx->mr_local_data[i].ibv->lkey;

        mr_idx++;
      }
    }

    // Post the write.
    int post_result = ibv_post_send(comm_ctx->qp, &wr[0], &bad_wr);
    if (post_result) {
      ulog_error("Posting send for data write failed with error %d.\n", post_result);
      return post_result;
    }
  }

  return 0;
}

int naaice_post_recv_mrsp(struct naaice_communication_context *comm_ctx) {
  ulog_trace("In naaice_post_recv_mrsp\n");

  // Receive work request and scatter/gather element.
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
  /* Only one receive request is needed for the data transfer: the server's
   * immediate write produces a single completion queue element. Posting more
   * would overflow the receive queue when using multiple output memory regions
   * over many RPC calls. */

  ulog_trace("In naaice_post_recv_data\n");

  // A single receive request.
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

  // Post the receive.
  int post_result = ibv_post_recv(comm_ctx->qp, &wr, &bad_wr);
  if (post_result) {
    ulog_error("Posting recieve for data failed with error %d.\n", post_result);
    return post_result;
  }

  return 0;
}
