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
 * naaice_swnaa.h
 *
 * Interface for NAAICE AP1 software dummy NAA (SWNAA).
 *
 * These functions are used to set up a software mockup of an NAA, which
 * behaves like a FPGA-based NAA should. Such an SWNAA can be used for testing
 * the NAAICE middleware layers in the absence of an FPGA.
 *
 * Florian Mikolajczak, florian.mikolajczak@uni-potsdam.de
 * Dylan Everingham, everingham@zib.de
 *
 * 26-01-2024
 *
 *****************************************************************************/

#ifndef NAAICE_SWNAA_H
#define NAAICE_SWNAA_H

/* Dependencies **************************************************************/

#include "naaice.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup PublicFunctionsSWNAA Functions
 */

/* Public Functions **********************************************************/

/** @addtogroup PublicFunctionsSWNAA
 *  @{
 */

/**
 * @brief Initialize a communication context structure.
 *
 * This function initializes a communication context structure.
 * The dummy software NAA reuses the communication context structure from the
 * host-side AP1 implementation, but does not use all fields in the same way.
 * In particular, the size and number of parameters are not known (and related
 * fields are not populated) until the MRSP has completed.
 *
 * @param comm_ctx
 *   Pointer to a communication context structure to be initialized.
 *   The pointer must not reference an existing structure; the structure is
 *   allocated and returned by this function.
 * @param port
 *   String specifying the connection port (e.g. `"12345"`).
 *
 * @return
 *   0 on success, -1 on failure.
 */
int naaice_swnaa_init_communication_context(
    struct naaice_communication_context **comm_ctx, uint16_t port);

/**
 * @brief Set up the software NAA connection.
 *
 * Polls for and handles connection events until the connection setup is
 * complete. Unlike the base naaice implementation, this function does not
 * require handling address or route resolution events. It does, however,
 * handle the “connection requests complete” event, which is not handled
 * on the host side.
 *
 * @param comm_ctx
 *   Pointer to the communication context structure describing the connection.
 *
 * @return
 *   0 on success, -1 on failure (e.g. due to timeout).
 */
int naaice_swnaa_setup_connection(
    struct naaice_communication_context *comm_ctx);

/**
 * @defgroup SWNAAEventHandlers Software NAA connection event handlers
 * @ingroup PublicFunctionsSWNAA
 *
 * These functions each handle a specific RDMA connection event. If the type
 * of the provided event matches the event type handled by the function, the
 * required logic is executed.
 *
 * After handling an event, flags in the communication context are updated to
 * reflect the current state of connection establishment.
 *
 * The events handled by these functions, in order, are:
 * - RDMA_CM_EVENT_CONNECTION_REQUEST
 * - RDMA_CM_EVENT_CONNECT_ESTABLISHED
 *
 * The following events are handled by naaice_swnaa_handle_error():
 * - RDMA_CM_EVENT_ADDR_ERROR
 * - RDMA_CM_EVENT_ROUTE_ERROR
 * - RDMA_CM_EVENT_CONNECT_ERROR
 * - RDMA_CM_EVENT_UNREACHABLE
 * - RDMA_CM_EVENT_REJECTED
 * - RDMA_CM_EVENT_DEVICE_REMOVAL
 * - RDMA_CM_EVENT_DISCONNECTED
 *
 * @param comm_ctx
 *   Pointer to the communication context structure describing the connection
 *   and maintaining connection state.
 * @param ev
 *   Pointer to the RDMA CM event to be checked and, if applicable, handled.
 *
 * @return
 *   0 on success (either the event was handled successfully or it was not of
 *   the matching type), -1 on failure.
 * @{
 */

/** @brief Handle RDMA_CM_EVENT_CONNECTION_REQUEST events. */
int naaice_swnaa_handle_connection_requests(
    struct naaice_communication_context *comm_ctx, struct rdma_cm_event *ev);
/** @brief Handle RDMA_CM_EVENT_CONNECT_ESTABLISHED events. */
int naaice_swnaa_handle_connection_established(
    struct naaice_communication_context *comm_ctx, struct rdma_cm_event *ev);
/** @brief Handle connection error events. */
int naaice_swnaa_handle_error(struct naaice_communication_context *comm_ctx,
                              struct rdma_cm_event *ev);

/** @} */
/**
 * @brief Poll for and handle a software NAA connection event.
 *
 * Polls the RDMA event channel stored in the communication context for a
 * connection event and handles it if one is received. This function delegates
 * the actual work to the corresponding poll and handler functions.
 *
 * @param comm_ctx
 *   Pointer to the communication context structure describing the connection.
 *
 * @return
 *   0 on success (regardless of whether an event was received),
 *   -1 on failure.
 */

int naaice_swnaa_poll_and_handle_connection_event(
    struct naaice_communication_context *comm_ctx);

/**
 * @brief Initialize MRSP on the software NAA side.
 *
 * Starts the MRSP on the NAA side by posting a receive for MRSP packets
 * expected from the host.
 *
 * @param comm_ctx
 *   Pointer to the communication context structure describing the connection
 *   and associated memory regions.
 *
 * @return
 *   0 on success, -1 on failure.
 */

int naaice_swnaa_init_mrsp(struct naaice_communication_context *comm_ctx);

/**
 * @brief Post a receive request for an MRSP message.
 *
 * Posts a receive request for an MRSP message. The receive request is added
 * to the queue and specifies the memory region to be written to (the MRSP
 * memory region).
 *
 * @param comm_ctx
 *   Pointer to the communication context structure describing the connection.
 *
 * @return
 *   0 on success, -1 on failure.
 */

int naaice_swnaa_post_recv_mrsp(struct naaice_communication_context *comm_ctx);

/**
 * @brief Handle a single work completion.
 *
 * Handles one work completion retrieved from the completion queue. Work
 * completions represent memory region write operations from the host to the
 * NAA or from the NAA to the host.
 *
 * @param comm_ctx
 *   Pointer to the communication context structure describing the connection.
 *
 * @return
 *   0 on success, -1 on failure.
 */
int naaice_swnaa_handle_work_completion(
    struct ibv_wc *wc, struct naaice_communication_context *comm_ctx);

/**
 * @defgroup SWNAAMRSPHandlers MRSP packet handlers
 * @ingroup PublicFunctionsSWNAA
 *
 * Handlers for MRSP packets of type *announce* and *announce-and-request*.
 * These functions process the contents of a received MRSP packet of the
 * corresponding type and populate the relevant fields in the communication
 * context.
 *
 * @param comm_ctx
 *   Pointer to the communication context structure describing the connection.
 *
 * @return
 *   0 on success, -1 on failure.
 * @{
 */

/** @brief Handle MRSP announce packets. */
int naaice_swnaa_handle_mr_announce(
    struct naaice_communication_context *comm_ctx);
/** @brief Handle MRSP announce-and-request packets. */
int naaice_swnaa_handle_mr_announce_and_request(
    struct naaice_communication_context *comm_ctx);

/** @} */

/**
 * @brief Send an MRSP message to the remote peer.
 *
 * Sends an MRSP packet to the remote peer using ibv_post_send() with
 * opcode IBV_WR_SEND.
 *
 * @param comm_ctx
 *   Pointer to the communication context structure describing the connection.
 * @param message_type
 *   Type of message to send. Must be one of:
 *   MSG_MR_ERR, MSG_MR_AAR, or MSG_MR_A.
 * @param errorcode
 *   Error code included in the packet if @p message_type is MSG_MR_ERR.
 *   Unused for other message types.
 *
 * @return
 *   0 on success, -1 on failure.
 */
int naaice_swnaa_send_message(struct naaice_communication_context *comm_ctx,
                              enum message_id message_type, uint8_t errorcode);

/**
 * @brief Post a receive for a memory region write.
 *
 * Posts a receive request for an RDMA memory region write. Only the final
 * memory region write (the one with an immediate value) requires a posted
 * receive. RDMA writes without an immediate occur without consuming a receive
 * request from the queue.
 *
 * The memory region specified in the receive request is the MRSP region.
 * This is a placeholder; the actual write destination is determined by the
 * sender.
 *
 * @param comm_ctx
 *   Pointer to the communication context structure describing the connection.
 *
 * @return
 *   0 on success, -1 on failure.
 */
int naaice_swnaa_post_recv_data(struct naaice_communication_context *comm_ctx);

/**
 * @brief Write a return memory region to the remote peer.
 *
 * Writes the return memory region, indicated by `comm_ctx->mr_return_idx`,
 * to the remote peer using `ibv_post_send()` with opcode
 * `IBV_WR_RDMA_WRITE_WITH_IMM`. The immediate value signals whether an error
 * occurred during computation (nonzero = error).
 *
 * @param comm_ctx
 *   Pointer to the communication context structure describing the connection.
 * @param fncode
 *   Function code for the NAA routine. Positive value indicates success,
 *   0 indicates an error.
 *
 * @return
 *   0 on success, -1 on failure.
 */
int naaice_swnaa_write_data(struct naaice_communication_context *comm_ctx,
                            uint8_t errorcode);

/**
 * @brief Disconnect and clean up the software NAA connection.
 *
 * Terminates the RDMA connection and frees all memory associated with the
 * communication context.
 *
 * @param comm_ctx
 *   Pointer to the communication context structure describing the connection.
 *
 * @return
 *   0 on success, -1 on failure.
 */
int naaice_swnaa_disconnect_and_cleanup(
    struct naaice_communication_context *comm_ctx);

/**
 * @brief Execute MRSP logic in a blocking manner.
 *
 * Performs all necessary MRSP processing in a blocking fashion, ensuring
 * that the procedure completes before returning.
 *
 * @param comm_ctx
 *   Pointer to the communication context structure describing the connection.
 *
 * @return
 *   0 on success, -1 on failure.
 */
int naaice_swnaa_do_mrsp(struct naaice_communication_context *comm_ctx);

/**
 * @brief Receive data transfer from the remote peer.
 *
 * Handles receiving data from the remote peer in a blocking manner. Updates
 * the communication context with information about the received data.
 *
 * @param comm_ctx
 *   Pointer to the communication context structure describing the connection.
 *
 * @return
 *   0 on success, -1 on failure.
 */
int naaice_swnaa_receive_data_transfer(
    struct naaice_communication_context *comm_ctx);

/**
 * @brief Perform the complete data transfer procedure.
 *
 * Executes all steps of the data transfer in a blocking manner, including:
 * - Receiving data from the NAA
 * - Waiting for the NAA computation to complete
 * - Writing the return data back to the remote peer
 *
 * @param comm_ctx
 *   Pointer to the communication context structure describing the connection.
 *
 * @return
 *   0 on success, -1 on failure.
 */
int naaice_swnaa_do_data_transfer(struct naaice_communication_context *comm_ctx,
                                  uint8_t errorcode);

/**
 * @brief Poll the completion queue in a non-blocking manner.
 *
 * Polls the completion queue for any work completions and handles them
 * using `naaice_swnaa_handle_work_completion` if received. Updates
 * `comm_ctx->state` to reflect the current status of the NAA connection and
 * routine.
 *
 * @param comm_ctx
 *   Pointer to the communication context structure describing the connection.
 *
 * @return
 *   0 on success (regardless of whether any work completions were received),
 *  -1 on failure.
 */
int naaice_swnaa_poll_cq_nonblocking(
    struct naaice_communication_context *comm_ctx);

/** @} */ // end of doxygen group PublicFunctionsSWNAA

#ifdef __cplusplus
}
#endif

#endif
