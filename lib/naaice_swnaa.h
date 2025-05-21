/**************************************************************************//**
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
extern "C"
{
#endif


/* Public Functions **********************************************************/

/**
 * naaice_init_communication_context:
 *  Initializes communication context struct.
 *  
 *  The dummy software NAA reuses the communication context struct from the
 *  host-side AP1 implementation, but doesn't use all the fields in the
 *  exact same way. Most importantly, the size and number of parameters
 *  is not known (and related fields are not populated) until after the
 *  MRSP is complete.
 * 
 * params:
 *  naaice_communication_context *comm_ctx: (return param)
 *    Pointer to communication context struct to be initialized.
 *    Should not point to an existing struct; the struct is allocated
 *    and returned by this function.
 *  const char *port:
 *    String specifying connection port, ex. "12345".
 * about:blank#blocked
 * returns:
 *  0 if sucessful, -1 if not.
 */
int naaice_swnaa_init_communication_context(
  struct naaice_communication_context **comm_ctx, uint16_t port);

/**
 * naaice_swnaa_setup_connection:
 *  Loops polling for and handling connection events until connection setup
 *  is complete. Unlike the base naaice version, does not require handling
 *  the address or route resolution events, but does handle the connection
 *  requests complete event which is not handled on the host side.
 * 
 * params:
 *  naaice_communication_context *comm_ctx:
 *    Pointer to struct describing the connection.
 * 
 * returns:
 *  0 if sucessful, -1 if not (due to timeout).
 */
int naaice_swnaa_setup_connection(
  struct naaice_communication_context *comm_ctx);
/**
 * swnaa connection event handlers:
 *  These functions each handle a specific connection event. If the provided
 *  event's type matches the event type of the handler function, it executes
 *  the necessary logic to handle it.
 *
 *  Subsequently, flags in the communication context are updated to represent
 *  the current status of connection establishment.
 *
 *  The events handled by these functions are, in order:
 *  RDMA_CM_EVENT_CONNECTION_REQUEST
 *  RDMA_CM_EVENT_CONNECT_ESTABLISHED
 *
 *  Also, the following are handled by naaice_swnaa_handle_error:
 *  RDMA_CM_EVENT_ADDR_ERROR, RDMA_CM_EVENT_ROUTE_ERROR,
 *  RDMA_CM_EVENT_CONNECT_ERROR, RDMA_CM_EVENT_UNREACHABLE,
 *  RDMA_CM_EVENT_REJECTED, RDMA_CM_EVENT_DEVICE_REMOVAL,
 *  RDMA_CM_EVENT_DISCONNECTED.
 *
 * params:
 *  naaice_communication_context *comm_ctx:
 *    Pointer to struct describing the connection with events to handle.
 *  struct rdma_cm_event *ev:
 *    Pointer to event to be checked and possibly handled.
 *
 * returns:
 *  0 if sucessful (i.e. either the event was the matching type and was handled
 *  successfully, or the event was not the matching type), -1 if not.
 */
int naaice_swnaa_handle_connection_requests(struct naaice_communication_context *comm_ctx,
                                struct rdma_cm_event *ev);
int naaice_swnaa_handle_connection_established(
    struct naaice_communication_context *comm_ctx, struct rdma_cm_event *ev);
int naaice_swnaa_handle_error(struct naaice_communication_context *comm_ctx, struct rdma_cm_event *ev);

    /**
     * naaice_swnaa_poll_and_handle_connection_event:
     *  Polls for a connection event on the RDMA event channel stored in the
     *  communication context and handles the event if one is received.
     *  Simply uses the poll and handle functions above.
     *
     * params:
     *  naaice_communication_context *comm_ctx:
     *    Pointer to struct describing the connection.
     *
     * returns:
     *  0 if sucessful (regardless of whether an event is received), -1 if not.
     */
    int
    naaice_swnaa_poll_and_handle_connection_event(
        struct naaice_communication_context *comm_ctx);

/**
 * naaice_swnaa_init_mrsp:
 *  Starts the MRSP on the NAA side. That is, posts a recieve for MRSP
 *  packets expected from the host.
 * 
 * params:
 *  naaice_communication_context *comm_ctx:
 *    Pointer to struct describing the connection and memory regions.
 * 
 * returns:
 *  0 if sucessful, -1 if not.
 */
int naaice_swnaa_init_mrsp(struct naaice_communication_context *comm_ctx);

/**
 * naaice_swnaa_post_recv_mrsp
 *  Posts a recieve for an MRSP message.
 *  A recieve request is added to the queue which specifies the memory region
 *  to be written to (in this case, the MRSP region).
 * 
 * params:
 *  naaice_communication_context *comm_ctx:
 *    Pointer to struct describing the connection.
 * 
 * returns:
 *  0 if successful, -1 if not.
 */
int naaice_swnaa_post_recv_mrsp(struct naaice_communication_context *comm_ctx);

/**
 * naaice_swnaa_handle_work_completion:
 *  Handles a single work completion from the completion queue.
 *  These represent memory region writes from host to NAA or NAA to host.
 * 
 * params:
 *  naaice_communication_context *comm_ctx:
 *    Pointer to struct describing the connection.
 * 
 * returns:
 *  0 if sucessful, -1 if not.
 */
int naaice_swnaa_handle_work_completion(struct ibv_wc *wc,
  struct naaice_communication_context *comm_ctx);

/**
 * naaice_swnaa_handle_mr_announce,
 * naaice_swnaa_handle_mr_announce_and_request:
 * 
 *  Handlers for MRSP packets.
 *  Processes the contents of a received MRSP packet of the corresponding type,
 *  populating relevant values in the communication context.
 * 
 * params:
 *  naaice_communication_context *comm_ctx:
 *    Pointer to struct describing the connection.
 * 
 * returns:
 *  0 if sucessful, -1 if not.
 */
int naaice_swnaa_handle_mr_announce(
  struct naaice_communication_context *comm_ctx);
int naaice_swnaa_handle_mr_announce_and_request(
  struct naaice_communication_context *comm_ctx);

/**
 * naaice_swnaa_send_message:
 *  Sends an MRSP packet to the remote peer. Done with a ibv_post_send using
 *  opcode IBV_WR_SEND.
 * 
 * params:
 *  naaice_communication_context *comm_ctx:
 *    Pointer to struct describing the connection.
 *  enum message_id message_type:
 *    Specifies the packet type. Should be one of MSG_MR_ERR, MSG_MR_AAR,
 *    or MSG_MR_A.
 *  uint8_t errorcode:
 *    Error code send in the packet, if message_type was MSG_MR_ERR.
 *    Unused for other message types.
 * 
 * returns:
 *  0 if sucessful, -1 if not.
 */
int naaice_swnaa_send_message(struct naaice_communication_context *comm_ctx,
  enum message_id message_type, uint8_t errorcode);

/**
 * naaice_swnaa_post_recv_data
 *  Posts a recieve for a memory region write.
 *  It is only necessary to post a recieve for the final memory region to be
 *  written, that is, the write with an immediate value. RDMA writes without
 *  an immediate simply occur without consuming a recieve request in the queue.
 *  
 *  The memory region specified in the recieve request is the MRSP region;
 *  this is just a dummy value, as the region written to is specified by the
 *  sender.
 * 
 * params:
 *  naaice_communication_context *comm_ctx:
 *    Pointer to struct describing the connection.
 * 
 * returns:
 *  0 if successful, -1 if not.
 */
int naaice_swnaa_post_recv_data(struct naaice_communication_context *comm_ctx);

/**
 * naaice_swnaa_write_data:
 *  Writes the return memory region, specified by comm_ctx->mr_return_idx, to
 *  the remote peer. Done with a ibv_post_send using opcode 
 *  IBV_WR_RDMA_WRITE_WITH_IMM. The immediate value indicates if an error has
 *  occured during calculation (nonzero = error).
 * 
 * params:
 *  naaice_communication_context *comm_ctx:
 *    Pointer to struct describing the connection.
 *  uint8_t fncode:
 *    Function Code for NAA routine. Positive, 0 on error.
 * 
 * returns:
 *  0 if sucessful, -1 if not.
 */
int naaice_swnaa_write_data(
  struct naaice_communication_context *comm_ctx, uint8_t errorcode);

/**
 * naaice_swnaa_disconnect_and_cleanup:
 *  Terminates the connection and frees all communication context memory.
 * 
 * params:
 *  naaice_communication_context *comm_ctx:
 *    Pointer to struct describing the connection.
 * 
 * returns:
 *  0 if sucessful, -1 if not.
 */
int naaice_swnaa_disconnect_and_cleanup(
  struct naaice_communication_context *comm_ctx);

/**
 * naaice_swnaa_do_mrsp
 *  Does all logic for the MRSP in a blocking fashion.
 *
 * params:
 *  naaice_communication_context *comm_ctx:
 *    Pointer to struct describing the connection.
 *
 * returns:
 *  0 if successful, -1 if not.
 */
int naaice_swnaa_do_mrsp(struct naaice_communication_context *comm_ctx);

/**
 * naaice_swnaa_receive_data_transfer
 *  Handles recieving data from remote peer, blocking until this is finished.
 *  Information about the data is updated in the communication context.
 *
 * params:
 *  naaice_communication_context *comm_ctx:
 *    Pointer to struct describing the connection.
 *
 * returns:
 *  0 if successful, -1 if not.
 */
int naaice_swnaa_receive_data_transfer(
  struct naaice_communication_context *comm_ctx);

/**
 * naaice_swnaa_do_data_transfer
 *  Does all logic for the data transfer, including recieving data from the 
 *  NAA, wating for the NAA calculation, and writing the return data back, 
 *  in a blocking fashion.
 *
 * params:
 *  naaice_communication_context *comm_ctx:
 *    Pointer to struct describing the connection.
 *
 * returns:
 *  0 if successful, -1 if not.
 */
int naaice_swnaa_do_data_transfer(
    struct naaice_communication_context *comm_ctx, uint8_t errorcode);

/**
 * naaice_swnaa_poll_cq_nonblocking:
 *  Polls the completion queue for any work completions, and handles them if
 *  any are received using naaice_handle_work_completion.
 *  
 *  Subsequently, comm_ctx->state is updated to reflect the current state
 *  of the NAA connection and routine.
 * 
 * params:
 *  naaice_communication_context *comm_ctx:
 *    Pointer to struct describing the connection.
 * 
 * returns:
 *  0 if sucessful (regardless of whether any work completions are received),
 * -1 if not.
 */
int naaice_swnaa_poll_cq_nonblocking(
  struct naaice_communication_context *comm_ctx);

#ifdef __cplusplus
}
#endif

#endif
