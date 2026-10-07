/*
 * Interface for the NAAICE AP1 software dummy NAA (SWNAA): a software mockup
 * that behaves like an FPGA-based NAA, for testing the NAAICE middleware layers
 * without an FPGA.
 */

#ifndef NAAICE_SWNAA_H
#define NAAICE_SWNAA_H

/* Dependencies **************************************************************/

#include <config.h>
#include <pthread.h>
#include <rdma/rdma_cma.h>
#include <stdint.h>
#include <sys/types.h>

#include "naaice.h"

/* Indices of free positions in the worker array. All positions below top are
 * available. */
struct connection_management {
  uint8_t connections[MAX_CONNECTIONS];
  volatile int top;
};

typedef uint8_t(rpc_function_t)(uint8_t fncode, struct naaice_communication_context *ctx);

struct context {
  pthread_mutex_t lock;
  uint8_t total_connections_lifetime;
  struct naaice_communication_context *master;
  struct connection_management *con_mng;
  struct naaice_communication_context *worker[MAX_CONNECTIONS];
  pthread_t worker_threads[MAX_CONNECTIONS];
  rpc_function_t *rpc_func;
};

struct worker_args {
  struct context *ctx;
  uint8_t worker_id;
};

#ifdef __cplusplus
extern "C" {
#endif

/* Public Functions
 * **********************************************************/

void *worker_procedure(void *args);

int naaice_swnaa_init_master(struct context **ctx, uint16_t local_cm_port,
                             rpc_function_t *rpc_func);

int naaice_swnaa_init_worker(struct context **ctx, uint8_t worker_id);

/*
 * Allocates and initializes a communication context structure, returned via
 * comm_ctx. The software NAA reuses the host-side context but not all fields
 * the same way; in particular the size and number of parameters are unknown
 * (and their fields unpopulated) until MRSP completes.
 * Returns 0 on success, -1 on failure.
 */
int naaice_swnaa_init_communication_context(struct naaice_communication_context **comm_ctx);

/*
 * Polls and handles connection events until setup is complete. Unlike the base
 * naaice implementation it does not handle address or route resolution events,
 * but does handle the connection-request event, which the host side does not.
 * Returns 0 on success, -1 on failure (e.g. timeout).
 */
int naaice_swnaa_setup_connection(struct context *ctx);

/*
 * Software NAA connection event handlers. Each handles one RDMA connection
 * event: if the event type matches, the handler runs and updates connection
 * state flags in the communication context. Each returns 0 on success
 * (including when the event did not match its type) and -1 on failure.
 *
 * Handled here, in order:
 * - RDMA_CM_EVENT_CONNECTION_REQUEST
 * - RDMA_CM_EVENT_CONNECT_ESTABLISHED
 *
 * Handled by naaice_swnaa_handle_error():
 * - RDMA_CM_EVENT_ADDR_ERROR
 * - RDMA_CM_EVENT_ROUTE_ERROR
 * - RDMA_CM_EVENT_CONNECT_ERROR
 * - RDMA_CM_EVENT_UNREACHABLE
 * - RDMA_CM_EVENT_REJECTED
 * - RDMA_CM_EVENT_DEVICE_REMOVAL
 * - RDMA_CM_EVENT_DISCONNECTED
 */

// Handle RDMA_CM_EVENT_CONNECTION_REQUEST events.
int naaice_swnaa_handle_connection_requests(struct context *ctx, struct rdma_cm_event *ev);
// Handle RDMA_CM_EVENT_CONNECT_ESTABLISHED events.
int naaice_swnaa_handle_connection_established(struct naaice_communication_context *comm_ctx,
                                               struct rdma_cm_event *ev);
// Handle connection error events.
int naaice_swnaa_handle_error(struct naaice_communication_context *comm_ctx,
                              struct rdma_cm_event *ev);

/*
 * Polls the RDMA event channel for a connection event and handles it if one
 * arrives, delegating to the matching poll and handler functions.
 * Returns 0 on success (whether or not an event was received), -1 on failure.
 */
int naaice_swnaa_poll_and_handle_connection_event(struct context *ctx);

/*
 * Starts MRSP on the NAA side by posting a receive for MRSP packets expected
 * from the host. Returns 0 on success, -1 on failure.
 */
int naaice_swnaa_init_mrsp(struct naaice_communication_context *comm_ctx);

/*
 * Posts a receive request for an MRSP message, targeting the MRSP memory
 * region. Returns 0 on success, -1 on failure.
 */
int naaice_swnaa_post_recv_mrsp(struct naaice_communication_context *comm_ctx);

/*
 * Handles one work completion from the completion queue. Work completions
 * represent memory region writes between host and NAA in either direction.
 * Returns 0 on success, -1 on failure.
 */
int naaice_swnaa_handle_work_completion(struct ibv_wc *wc,
                                        struct naaice_communication_context *comm_ctx);

/*
 * Handlers for MRSP packets of type announce and announce-and-request. Each
 * processes a received packet of its type and populates the relevant fields in
 * the communication context. Each returns 0 on success, -1 on failure.
 */

// Handle MRSP announce packets.
int naaice_swnaa_handle_mr_announce(struct naaice_communication_context *comm_ctx);
// Handle MRSP announce-and-request packets.
int naaice_swnaa_handle_mr_announce_and_request(struct naaice_communication_context *comm_ctx);

/*
 * Sends an MRSP packet to the remote peer via ibv_post_send() with opcode
 * IBV_WR_SEND. message_type must be MSG_MR_ERR, MSG_MR_AAR, or MSG_MR_A.
 * errorcode is included only for MSG_MR_ERR and ignored otherwise.
 * Returns 0 on success, -1 on failure.
 */
int naaice_swnaa_send_message(struct naaice_communication_context *comm_ctx,
                              enum message_id message_type, uint8_t errorcode);

/*
 * Posts a receive request for an RDMA memory region write. Only the final
 * write (the one carrying an immediate) needs a posted receive; writes without
 * an immediate do not consume a receive request. The region named in the
 * request is the MRSP region as a placeholder; the actual destination is
 * chosen by the sender. Returns 0 on success, -1 on failure.
 */
int naaice_swnaa_post_recv_data(struct naaice_communication_context *comm_ctx);

/*
 * Writes the return memory region (comm_ctx->mr_return_idx) to the remote peer
 * via ibv_post_send() with opcode IBV_WR_RDMA_WRITE_WITH_IMM. A nonzero
 * errorcode signals that an error occurred during computation.
 * Returns 0 on success, -1 on failure.
 */
int naaice_swnaa_write_data(struct naaice_communication_context *comm_ctx, uint8_t errorcode);

/*
 * Terminates the RDMA connection and frees all memory associated with the
 * communication context. Returns 0 on success, -1 on failure.
 */
int naaice_swnaa_disconnect_and_cleanup(struct naaice_communication_context *comm_ctx);

/*
 * Performs all MRSP processing, blocking until the procedure completes.
 * Returns 0 on success, -1 on failure.
 */
int naaice_swnaa_do_mrsp(struct naaice_communication_context *comm_ctx);

/*
 * Receives data from the remote peer (blocking) and updates the communication
 * context with information about it. Returns 0 on success, -1 on failure.
 */
int naaice_swnaa_receive_data_transfer(struct naaice_communication_context *comm_ctx);

/*
 * Runs the complete data transfer procedure (blocking): receive data from the
 * NAA, wait for the computation to finish, and write the return data back to
 * the remote peer. Returns 0 on success, -1 on failure.
 */
int naaice_swnaa_do_data_transfer(struct naaice_communication_context *comm_ctx, uint8_t errorcode);

/*
 * Polls the completion queue (non-blocking) and handles any work completions
 * via naaice_swnaa_handle_work_completion, updating comm_ctx->state to reflect
 * the current NAA connection and routine status.
 * Returns 0 on success (whether or not any completions were received),
 * -1 on failure.
 */
int naaice_swnaa_poll_cq_nonblocking(struct naaice_communication_context *comm_ctx);

#ifdef __cplusplus
}
#endif

#endif
