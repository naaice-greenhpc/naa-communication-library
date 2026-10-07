/*
 * Interface for the NAAICE AP1 communication layer.
 *
 * Acronyms: MR (Memory Region), MRSP (Memory Region Setup Protocol),
 * RPC (Remote Procedure Call), RDMA (Remote Direct Memory Access),
 * IBV (InfiniBand Verbs).
 */

#ifndef NAAICE_H
#define NAAICE_H

#ifdef __cplusplus
#include <atomic>
#define ATOMIC_TYPE(T) std::atomic<T>
#else
#include <stdatomic.h>
#define ATOMIC_TYPE(T) _Atomic(T)
#endif

/*
 * AP1 Usage
 * For standard operation, functions should be called in the following order:
 *
 * 1. naaice_init_communication_context
 * 2. (in any order)
 *    naaice_setup_connection
 *    naaice_register_mrs
 *    naaice_set_internal_mrs (optional)
 *    naaice_set_input_mr
 *    naaice_set_output_mr
 *    naaice_set_singlesend_mr (optional)
 * 3. naaice_set_bytes_to_send (optional)
 * 4. naaice_do_mrsp
 * 5. naaice_do_data_transfer
 * 6. naaice_disconnect_and_cleanup
 *
 * naaice_do_mrsp blocks until MRSP is complete. For a non-blocking version,
 * use instead:
 *
 * naaice_init_mrsp
 * while (communication_context->state < MRSP_DONE) {
 *  naaice_poll_cq_nonblocking }
 *
 * naaice_do_data_transfer blocks until MRSP is complete. For a non-blocking
 * version, use instead:
 *
 * naaice_init_data_transfer
 * while (communication_context->state < FINISHED) {
 *  naaice_poll_cq_nonblocking }
 */

/* Dependencies **************************************************************/

#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <infiniband/verbs.h>
#include <inttypes.h>
#include <limits.h>
#include <netdb.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <ulog.h>
#include <unistd.h>

/* Constants *****************************************************************/
#define SERVER_CONNECTION_PORT 12345

#define RX_DEPTH 1025
#define ntohll be64toh
#define htonll htobe64

// Timeout for resolving RDMA routes, in milliseconds.
#define TIMEOUT_RESOLVE_ROUTE 500

// Timeout for poll functions, in milliseconds.
#define POLLING_TIMEOUT 500

// Default timeout for blocking calls to the NAA, in seconds.
#define DEFAULT_TIMEOUT 3600

// Default number of retries for the RDMA connection.
#define DEFAULT_RETRY_COUNT 7
// 4.096 us * 2^12 = 16.8 ms per retry, 117 ms for all seven. The kernel hands RoCE its InfiniBand
// default of 18, which is 1.07 s per retry: longer than POLLING_TIMEOUT allows a whole transfer.
#define DEFAULT_ACK_TIMEOUT 12

// Host RoCE IP TOS. ECN bits 0b10 = ECT(0) (egress ECN-capable); DSCP 0 — set to the
// fabric's RoCE DSCP if the switches classify by DSCP.
#define NAAICE_ROCE_TOS 0x02

// Maximum allowed number of memory regions.
// Total of parameters and internal NAA memory regions.
#define MAX_MRS 32

// Immediate return code offset
#define IMMEDIATE_OFFSET 8

// Size of memory region used for MRSP.
// Simply the size of a memory region advertisement and request message
// multiplied by the maximum allowed number of memory regions.
// TODO(#5): add header size.
#define MR_SIZE_MRSP sizeof(struct naaice_mr_advertisement_request) * MAX_MRS

// FPGA addresses are sent in a 7-byte region, so this is the maximum value
// the can take (2^56-1)

/* Structs and Enums *********************************************************/

// Message IDs for packets exchanged between host and NAA during MRSP and other
// control communication.
enum message_id {
  // Error message.
  MSG_MR_ERR = 0x00,

  // Memory region advertisement and request, sent by host.
  MSG_MR_AAR = 0x01,

  // Memory region advertisement without request, usually sent by NAA.
  MSG_MR_A = 0x02,

  // Reserved/unused message ID.
  MSG_MR_R = 0x03,
};

// A memory region advertised to a remote peer during MRSP.
struct naaice_mr_advertisement {
  uint64_t addr;

  // Remote key for RDMA access.
  uint32_t rkey;

  // Size in bytes.
  uint32_t size;
};

// A memory region that is both advertised and requested during MRSP.
// TODO(#5): could embed a naaice_mr_advertisement as a member.
struct naaice_mr_advertisement_request {
  union {
    // Packed flags + requested NAA address.
    uint64_t mr_info;

    // Byte 0 holds the MR flags, bytes 1-7 hold the requested NAA MR address.
    uint8_t mr_info_bytearray[8];
  };

  // Requested NAA memory region address.
  uint64_t addr;

  // Remote key for RDMA access.
  uint32_t rkey;

  // Size in bytes.
  uint32_t size;
};

// Flags carried in naaice_mr_advertisement_request describing a region to the NAA.
enum naaice_mrflags_value {

  // Internal to the NAA only; not transferred.
  MRFLAG_INTERNAL = 0x01,

  // Sent only on the first RPC; subsequent RPCs skip it.
  MRFLAG_SINGLESEND = 0x02,

  // RPC input parameter.
  MRFLAG_INPUT = 0x04,

  // RPC output parameter.
  MRFLAG_OUTPUT = 0x08,
};

// A request for the NAA to allocate a memory region of the given size.
struct naaice_mr_request {
  // Requested size in bytes.
  uint64_t size;
};

// An error message exchanged between host and NAA during MRSP.
struct naaice_mr_error {
  uint8_t code;

  // Padding for alignment.
  uint8_t padding[2];
};

// Structs used as headers for the above messages.
// TODO(#5): could these be folded into the message structs?
struct naaice_mr_hdr {
  uint8_t type;
};

struct naaice_mr_dynamic_hdr {
  uint8_t count;
  uint8_t padding[2];
};

// Metadata for a memory region located on the remote NAA.
struct naaice_mr_peer {
  union {
    uint64_t addr;

    // Byte view of the remote address (e.g. FPGA address).
    uint8_t fpgaaddr[8];
  };

  // Remote key for RDMA access.
  uint32_t rkey;

  // Size in bytes.
  size_t size;

  // Region should be written back from the NAA.
  bool to_write;

  // Single-send region (written only once).
  bool single_send;
};

// Metadata for a memory region located on the host.
struct naaice_mr_local {
  // Registered IBV memory region.
  struct ibv_mr *ibv;

  // Local user-space address.
  char *addr;

  // Size in bytes.
  size_t size;

  // Region should be written to the remote NAA.
  bool to_write;

  // Single-send region (written only once).
  bool single_send;
};

// A memory region used internally for computation on the NAA. These regions
// exist only on the NAA side and are not transferred during data transfer.
struct naaice_mr_internal {
  union {
    // Address in NAA memory space.
    uint64_t addr;

    // Byte view of the address (e.g. FPGA address).
    uint8_t fpgaaddr[8];
  };

  // Size in bytes.
  size_t size;
};

// Per-RPC metadata stored in a dedicated memory region.
struct naaice_rpc_metadata {
  // Address where the RPC return value should be written.
  uintptr_t return_addr;
};

#ifdef REMOTE_MEMORY_MANAGEMENT_SERVICE
// Connection data for the rmms: address, port, and the assigned rmms_id.
struct naaice_rmms_data {
  // id given by the rmms
  // set on first request
  int rmms_id;

  // port rmms is listening on
  // set on init
  int rmms_port;

  // rmms address
  // set on init
  char *rmms_server_address;

  // identifier of the fpga
  // set on init
  char *fpga_mnemonic;

  // identifier of memory type
  // set on init
  char *mem_type;
};
#endif

/*
 * Connection state machine for NAA communication, progressing from connection
 * establishment through MRSP and data transfer to completion. The flow differs
 * slightly between host and NAA.
 *
 * Host states:
 * - NAAICE_INIT: Starting state.
 * - NAAICE_READY: Address resolved.
 * - NAAICE_CONNECTED: Connection established.
 * - NAAICE_MRSP_SENDING: Posting write for MRSP packet.
 * - NAAICE_MRSP_RECEIVING: Waiting for / processing MRSP response from NAA.
 * - NAAICE_MRSP_DONE: Finished MRSP.
 * - NAAICE_DATA_SENDING: Posting write for data transfer to NAA.
 * - NAAICE_DATA_RECEIVING: Waiting for / processing data transfer back from NAA.
 * - NAAICE_FINISHED: Done.
 *
 * NAA states:
 * - NAAICE_INIT: Starting state.
 * - NAAICE_CONNECTED: Connection established.
 * - NAAICE_MRSP_RECEIVING: Waiting for / processing MRSP packet from host.
 * - NAAICE_MRSP_SENDING: Posting write for MRSP packet.
 * - NAAICE_MRSP_DONE: Finished MRSP.
 * - NAAICE_DATA_RECEIVING: Waiting for / processing data transfer from host.
 * - NAAICE_CALCULATING: Running RPC.
 * - NAAICE_DATA_SENDING: Posting write for data transfer back to host.
 * - NAAICE_FINISHED: Done.
 */
typedef enum {
  NAAICE_INIT = 0,             // Starting state
  NAAICE_READY = 1,            // Address resolved (host only)
  NAAICE_CONNECTED = 2,        // Connection established
  NAAICE_DISCONNECTED = 3,     // Connection disconnected
  NAAICE_MRSP_SENDING = 10,    // Posting write for MRSP packet
  NAAICE_MRSP_RECEIVING = 11,  // Waiting for / processing MRSP packet
  NAAICE_MRSP_DONE = 12,       // Finished MRSP
  NAAICE_DATA_SENDING = 20,    // Posting write for data transfer
  NAAICE_CALCULATING = 21,     // Running RPC on NAA
  NAAICE_DATA_RECEIVING = 22,  // Waiting for / processing data transfer
  NAAICE_FINISHED = 30,        // Completed all communication
  NAAICE_ERROR = 40,           // Error state
} naaice_communication_state;

/*
 * Holds everything about a connection: RDMA resources, memory regions, current
 * state, function codes, and transfer metadata. Passed to almost all AP1 functions.
 */
struct naaice_communication_context {
  /* --- Basic connection properties --- */

  // RDMA communication identifier.
  struct rdma_cm_id *id;

  // RDMA event channel.
  struct rdma_event_channel *ev_channel;

  // IBV device context.
  struct ibv_context *ibv_ctx;

  // Protection domain for memory regions.
  struct ibv_pd *pd;

  // Completion channel.
  struct ibv_comp_channel *comp_channel;

  // Completion queue.
  struct ibv_cq *cq;

  // Queue pair.
  struct ibv_qp *qp;

  // Operation timeout in seconds.
  double timeout;

  // Retry count for the RDMA connection.
  uint8_t retry_count;

  // Max bytes sent inline: the user buffer is copied into the WQE, which is
  // faster for small messages.
  uint16_t max_inline_data;

  /* --- Current connection state --- */

  ATOMIC_TYPE(naaice_communication_state) state;

  /* --- Local memory regions --- */

  struct naaice_mr_local *mr_local_data;

  uint8_t no_local_mrs;

  // Index of the return memory region (1..no_local_mrs), set by naaice_set_metadata.
  uint8_t mr_return_idx;

  /* --- Peer memory regions --- */

  // Peer memory regions representing symmetric parameters.
  struct naaice_mr_peer *mr_peer_data;

  uint8_t no_peer_mrs;

  /* --- MRSP-related memory --- */

  // Local memory region used for MRSP messages.
  struct naaice_mr_local *mr_local_message;

  /* --- Internal memory regions on NAA --- */

  struct naaice_mr_internal *mr_internal;

  uint8_t no_internal_mrs;

  /* --- Function and return codes --- */

  // Function code specifying which NAA routine to call.
  uint8_t fncode;

  // User immediate value from the response (24 bits).
  uint32_t response_user_immediate;

  /* --- Transfer tracking --- */

  // RDMA writes performed to the NAA.
  uint8_t rdma_writes_done;

  // Response from the NAA seen.
  bool response_received;

  // Bytes received from the NAA.
  uint32_t bytes_received;

  uint8_t no_input_mrs;

  uint8_t no_output_mrs;

  // RPC calls performed on this connection.
  unsigned int no_rpc_calls;

  /* --- Immediate value for RDMA transfers --- */

  // 32-bit immediate sent during RDMA transfers. Byte 0 holds the function code
  // (set by naaice_init_communication_context); the rest are set via naaice_set_immediate.
  union {
    uint32_t immediate;
    uint8_t immediate_bytearr[4];
  };

  /* --- Connection tracking for the server --- */
  uint8_t connection_id;

#ifdef REMOTE_MEMORY_MANAGEMENT_SERVICE
  // Information for the rmms.
  struct naaice_rmms_data *rmms_data;
#endif
};

#ifdef __cplusplus
extern "C" {
#endif

/* Public Functions **********************************************************/

/*
 * Initializes the communication context, generating sequential NAA memory
 * addresses starting from addr_offset. After this call the context is ready to
 * pass to all other API functions. Returns 0 on success, -1 on failure.
 *
 * comm_ctx must not point to an existing struct; the struct is allocated and
 *   returned through this double pointer.
 * param_sizes / internal_mr_sizes are in bytes.
 * params must be preallocated by the host application.
 * local_address (e.g. "10.3.10.136") is optional; pass NULL to skip it. Do not
 *   provide one in loopback mode.
 * remote_cm_port must match the server's (NAA or SW NAA) local port.
 */
int naaice_init_communication_context(struct naaice_communication_context **comm_ctx,
                                      uint64_t addr_offset, size_t *param_sizes, char **params,
                                      unsigned int params_amount, unsigned int internal_mr_amount,
                                      size_t *internal_mr_sizes, uint8_t fncode,
                                      const char *local_address, const char *remote_address,
                                      uint16_t remote_cm_port);

int naaice_init_communication_context_with_addresses(
    struct naaice_communication_context **comm_ctx, uint64_t *naa_addresses, char **params,
    unsigned int params_amount, size_t *mr_sizes, unsigned int internal_mr_amount, uint8_t fncode,
    const char *local_address, const char *remote_address, uint16_t remote_cm_port);

#ifdef REMOTE_MEMORY_MANAGEMENT_SERVICE
/*
 * Like naaice_init_communication_context, but NAA memory addresses come from
 * the remote memory management service (rmms), and all rmms communication info
 * is set up too. After this call the context is ready to pass to all other API
 * functions. Returns 0 on success, -1 on failure.
 *
 * comm_ctx must not point to an existing struct; the struct is allocated and
 *   returned through this double pointer.
 * param_sizes / internal_mr_sizes are in bytes.
 * params must be preallocated by the host application.
 * local_address (e.g. "10.3.10.136") is optional; pass NULL to skip it. Do not
 *   provide one in loopback mode.
 * remote_cm_port must match the server's (NAA or SW NAA) local port.
 */
int naaice_init_communication_context_with_rmms(
    struct naaice_communication_context **comm_ctx, size_t *param_sizes, char **params,
    unsigned int params_amount, unsigned int internal_mr_amount, size_t *internal_mr_sizes,
    uint8_t fncode, const char *local_address, const char *remote_address, uint16_t remote_cm_port,
    struct naaice_rmms_data *rmms_data);

int naaice_init_rmms_data(struct naaice_rmms_data **rmms_data, const char *remote_malloc_address,
                          uint16_t remote_malloc_port, const char *fpga_key, const char *mem_type);
#endif

/*
 * Polls the RDMA event channel in the context; any received connection event is
 * stored in ev. Returns 0 on success (whether or not an event arrived), -1 on
 * failure.
 */
int naaice_poll_connection_event(struct naaice_communication_context *comm_ctx,
                                 struct rdma_cm_event *ev, struct rdma_cm_event *ev_cp);

/*
 * Connection event handlers for RDMA connection management events. Each handler
 * runs its logic only if ev's type matches, then updates flags in the context to
 * reflect connection-establishment progress. Returns 0 on success (handled, or no
 * type match), -1 on failure.
 *
 * Handled here, in order:
 * - RDMA_CM_EVENT_ADDR_RESOLVED
 * - RDMA_CM_EVENT_ROUTE_RESOLVED
 * - RDMA_CM_EVENT_CONNECT_ESTABLISHED
 *
 * Handled by naaice_handle_error:
 * - RDMA_CM_EVENT_ADDR_ERROR
 * - RDMA_CM_EVENT_ROUTE_ERROR
 * - RDMA_CM_EVENT_CONNECT_ERROR
 * - RDMA_CM_EVENT_UNREACHABLE
 * - RDMA_CM_EVENT_REJECTED
 * - RDMA_CM_EVENT_DEVICE_REMOVAL
 * - RDMA_CM_EVENT_DISCONNECTED
 */

// Handles RDMA_CM_EVENT_ADDR_RESOLVED.
int naaice_handle_addr_resolved(struct naaice_communication_context *comm_ctx,
                                struct rdma_cm_event *ev);

// Handles RDMA_CM_EVENT_ROUTE_RESOLVED.
int naaice_handle_route_resolved(struct naaice_communication_context *comm_ctx,
                                 struct rdma_cm_event *ev);

// Handles RDMA_CM_EVENT_CONNECT_ESTABLISHED.
int naaice_handle_connection_established(struct naaice_communication_context *comm_ctx,
                                         struct rdma_cm_event *ev);

/*
 * Handles RDMA connection error events: RDMA_CM_EVENT_ADDR_ERROR,
 * RDMA_CM_EVENT_ROUTE_ERROR, RDMA_CM_EVENT_CONNECT_ERROR,
 * RDMA_CM_EVENT_UNREACHABLE, RDMA_CM_EVENT_REJECTED, RDMA_CM_EVENT_DEVICE_REMOVAL
 * and RDMA_CM_EVENT_DISCONNECTED.
 */
int naaice_handle_error(struct naaice_communication_context *comm_ctx, struct rdma_cm_event *ev);

// Handles all other unsupported or unexpected RDMA CM events.
int naaice_handle_other(struct naaice_communication_context *comm_ctx, struct rdma_cm_event *ev);

/*
 * Polls the RDMA event channel and dispatches any received event to the right
 * connection event handler. Convenience wrapper over the poll and handler
 * functions above. Returns 0 on success (whether or not an event arrived), -1 on
 * failure.
 */
int naaice_poll_and_handle_connection_event(struct naaice_communication_context *comm_ctx);

/*
 * Sets up the RDMA connection, blocking until setup completes by repeatedly
 * calling naaice_poll_and_handle_connection_event. Returns 0 on success, -1 on
 * failure (e.g. timeout).
 */
int naaice_setup_connection(struct naaice_communication_context *comm_ctx);

/*
 * Registers local memory regions via ibv_reg_mr: input/output parameters, the
 * metadata region, and the MRSP region. On registration error the remote peer
 * is notified via naaice_send_message. Returns 0 on success, -1 on failure.
 */
int naaice_register_mrs(struct naaice_communication_context *comm_ctx);

/*
 * Allocates and initializes descriptors for parameter (non-internal) memory
 * regions. Called from naaice_init_communication_context. Returns 0 on success,
 * -1 on failure.
 *
 * local_addrs are user-space addresses corresponding to the params array passed
 *   to naaice_init_communication_context.
 * remote_addrs are NAA memory addresses obtained from the memory management
 *   service where the regions are requested to be stored.
 * sizes are in bytes, corresponding to param_sizes.
 */
int naaice_set_parameter_mrs(struct naaice_communication_context *comm_ctx,
                             unsigned int n_parameter_mrs, uint64_t *local_addrs,
                             uint64_t *remote_addrs, size_t *sizes);

/*
 * Mark a memory region as an input and/or output parameter. Input regions are
 * written to the NAA during data transfer; output regions are written back from
 * it. Must be called before naaice_init_mrsp. Return 0 on success, -1 on failure.
 */

// Marks a memory region as an input parameter.
int naaice_set_input_mr(struct naaice_communication_context *comm_ctx, unsigned int input_mr_idx);

// Marks a memory region as an output parameter.
int naaice_set_output_mr(struct naaice_communication_context *comm_ctx, unsigned int output_mr_idx);

/*
 * Marks a memory region as single-send: written exactly once during the first
 * RPC on the connection and skipped on later RPCs. If the region isn't already
 * an input region, it is marked as one. Must be called before naaice_init_mrsp.
 * Returns 0 on success, -1 on failure.
 */
int naaice_set_singlesend_mr(struct naaice_communication_context *comm_ctx,
                             unsigned int singlesend_mr_idx);

/*
 * Adds internal memory regions to the context. These exist only on the NAA side
 * for computation and are not transferred; they are included in the MR
 * announcement so the NAA allocates them. Must be called before naaice_init_mrsp.
 * Call only once per context; each call overwrites the previous internal MR info.
 * Returns 0 on success, -1 on failure.
 *
 * addrs are NAA memory addresses requested of the NAA during MRSP.
 * sizes are in bytes.
 */
int naaice_set_internal_mrs(struct naaice_communication_context *comm_ctx,
                            unsigned int n_internal_mrs, uint64_t *addrs, size_t *sizes);

/*
 * Sets the immediate value written during data transfer: up to 3 user bytes go
 * in the upper 3 bytes, while the lowest byte is reserved for the function code.
 * Must be called before naaice_init_data_transfer. Returns 0 on success, -1 on
 * failure.
 *
 * imm_bytes holds at most 3 bytes.
 */
int naaice_set_immediate(struct naaice_communication_context *comm_ctx, uint8_t *imm_bytes);

/*
 * Starts the Memory Region Setup Protocol (MRSP): sends advertise/request
 * packets and posts a receive for the response, preparing the connection for
 * data transfer. Returns 0 on success, -1 on failure.
 */
int naaice_init_mrsp(struct naaice_communication_context *comm_ctx);

/*
 * Starts the data transfer to the NAA by posting write operations for the
 * memory regions, preparing the connection for remote computation. Returns 0 on
 * success, -1 on failure.
 */
int naaice_init_data_transfer(struct naaice_communication_context *comm_ctx);

/*
 * Processes a single work completion from the completion queue, typically a
 * memory region write between host and NAA. Returns 0 on success, -1 on failure.
 */
int naaice_handle_work_completion(struct ibv_wc *wc, struct naaice_communication_context *comm_ctx);

/*
 * Polls the completion queue without blocking, processing any completions via
 * naaice_handle_work_completion and updating comm_ctx->state afterward. Returns
 * 0 on success (whether or not completions arrived), -1 on failure.
 */
int naaice_poll_cq_nonblocking(struct naaice_communication_context *comm_ctx);

/*
 * Polls the completion queue, blocking until at least one completion is
 * available, then processes it via naaice_handle_work_completion and updates
 * comm_ctx->state. Returns 0 on success, -1 on failure.
 */
int naaice_poll_cq_blocking(struct naaice_communication_context *comm_ctx);

/*
 * Like naaice_poll_cq_blocking but busy-waits for a completion instead of
 * blocking on the channel. Processes it via naaice_handle_work_completion and
 * updates comm_ctx->state. Returns 0 on success, -1 on failure.
 */
int naaice_poll_cq_busy(struct naaice_communication_context *comm_ctx);

/*
 * Terminates the RDMA connection and frees all memory associated with the
 * communication context. Returns 0 on success, -1 on failure.
 */
int naaice_disconnect_and_cleanup(struct naaice_communication_context *comm_ctx);

/*
 * Sends an MRSP packet to the remote peer via ibv_post_send with opcode
 * IBV_WR_SEND. Returns 0 on success, -1 on failure.
 *
 * message_type is one of MSG_MR_ERR, MSG_MR_AAR, or MSG_MR_A.
 * errorcode is included only when message_type is MSG_MR_ERR; otherwise ignored.
 */
int naaice_send_message(struct naaice_communication_context *comm_ctx, enum message_id message_type,
                        uint8_t errorcode);

/*
 * Writes memory regions (metadata and input parameters) to the NAA, using
 * ibv_post_send with IBV_WR_RDMA_WRITE for regular regions and
 * IBV_WR_RDMA_WRITE_WITH_IMM for the final region. Returns 0 on success, -1 on
 * failure.
 *
 * fncode is the NAA routine code; must be positive (0 indicates an error).
 */
int naaice_write_data(struct naaice_communication_context *comm_ctx, uint8_t fncode);

/*
 * Posts a receive request for an MRSP message, targeting the MRSP region.
 * Returns 0 on success, -1 on failure.
 */
int naaice_post_recv_mrsp(struct naaice_communication_context *comm_ctx);

/*
 * Posts a receive request for a memory region write. Only the final write (the
 * one carrying an immediate) consumes a receive request; plain RDMA writes do
 * not. The region named in the request is the MRSP region, a placeholder since
 * the sender determines the region actually written. Returns 0 on success, -1
 * on failure.
 */
int naaice_post_recv_data(struct naaice_communication_context *comm_ctx);

/*
 * Allocates and initializes RDMA resources: protection domain, completion
 * channel, completion queue, and queue pair. Returns 0 on success, -1 on failure.
 */
int naaice_init_rdma_resources(struct naaice_communication_context *comm_ctx);

/*
 * Runs the full Memory Region Setup Protocol (MRSP) and blocks until memory
 * regions are advertised, requested, and acknowledged. Returns 0 on success, -1
 * on failure.
 */
int naaice_do_mrsp(struct naaice_communication_context *comm_ctx);

/*
 * Runs the complete data transfer and blocks: writes data to the NAA, waits for
 * computation to finish, and receives the result back. Returns 0 on success, -1
 * on failure.
 */
int naaice_do_data_transfer(struct naaice_communication_context *comm_ctx);

/*
 * Sets how many bytes to send from the given memory region during data transfer.
 * number_bytes of 0 resets to the region's original size. Returns 0 on success,
 * -1 on failure.
 */
int naaice_set_bytes_to_send(struct naaice_communication_context *comm_ctx, int mr_idx,
                             int number_bytes);

#ifdef __cplusplus
}
#endif

#endif
