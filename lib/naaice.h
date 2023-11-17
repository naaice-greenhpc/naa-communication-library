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
 * naaice.h
 *
 * Interface for NAAICE AP1 communication layer.
 * 
 * Acronyms used in the NAAICE documentation:
 *  MR: Memory Region
 *  MRSP: Memory Region Setup Protocol
 *  RPC: Remote Procedure Call
 *  RDMA: Remote Direct Memory Access
 *  IBV: InfiniBand Verbs
 * 
 * Florian Mikolajczak, florian.mikolajczak@uni-potsdam.de
 * Dylan Everingham, everingham@zib.de
 * 
 * 08-11-2023
 * 
 *****************************************************************************/

#ifndef NAAICE_H
#define NAAICE_H

/** 
 * TODO:
 * 
 * (functionality)
 * 
 * - Consider the use of posix_memalign in naaice_init_communication_context.
 *    This is a reallocation of the input parameters. Should we instead
 *    require that the user ensure memory alignment, or find a way to ensure it
 *    without reallocation?
 * 
 * - Add support for running multiple RPCs before disconnecting.
 * 
 * - Add support for routine-specific metadata fields.
 * 
 * - Add timeouts.
 *
 * - Handle errors better, utilizing errno and ERROR state in state machine.
 * 
 * (style)
 * 
 * - Unify some of the packet structs used in naaice_send_message, so that a
 *    single type can be used rather than concatenating headers, for example.
 * 
 * - Make type usage more uniform, i.e. single types for MR addr pointers and
 *    sizes.
 * 
 */

/**
 * AP1 Usage
 * For standard operation, functions should be called in the following order:
 * 
 * 1. naaice_init_communication_context
 * 2. (in any order)
 *    naaice_setup_connection
 *    naaice_register_mrs
 *    naaice_set_metadata
 *    naaice_set_internal_mrs
 * 3. naaice_do_mrsp
 * 4. naaice_do_data_transfer
 * 5. naaice_disconnect_and_cleanup
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

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <unistd.h>
#include <sys/socket.h>
#include <limits.h>
#include <netdb.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>


/* Constants *****************************************************************/

#define RX_DEPTH 1025
#define ntohll be64toh
#define htonll htobe64
#define TIMEOUT_RESOLVE_ROUTE 500 // in ms.

// Maximum allowed number of memory regions.
// Total of parameters and internal NAA memory regions.
#define MAX_MRS 32

// size of MR for MR exchange
// type + padding: 32 + Maximum number of regions:UINT_MAX+1
//  * (Addr:64 +  size:32 + key:32) + requested size:64 / 8
// TODO: rewrite in terms of struct sizes.
//#define MR_SIZE_MR_EXHANGE (32 + (UINT8_MAX + 1) * 2 * 64 + 64) / 8

// Size of memory region used for MRSP.
// Simply the size of a memory region advertisement and request message
// multiplied by the maximum allowed number of memory regions.
// TODO: add header size.
#define MR_SIZE_MRSP sizeof(struct naaice_mr_advertisement_request) * MAX_MRS

/* Structs and Enums *********************************************************/

// ID used to identifiy packets sent between host and NAA.
enum message_id {
  MSG_MR_ERR = 0x00,  // Error.
  MSG_MR_AAR = 0x01,  // MR advertisement and request, sent by host.
  MSG_MR_A = 0x02,    // MR advertisement without request, usually sent by NAA.
  MSG_MR_R = 0x03,    // Unused.
};

// Struct to hold memory region advertisement message.
struct naaice_mr_advertisement
{
  uint64_t addr;
  uint32_t rkey;
  uint32_t size;
};

// Struct to hold memory region advertisement and request message.
// TODO: Could have a naaice_mr_advertisement struct as member.
struct naaice_mr_advertisement_request{
  uint8_t mrflags;
  uint8_t fpgaaddress[7];
  uint64_t addr;
  uint32_t rkey;
  uint32_t size;
};

// Enum with values for naaice_mr_advertisement_request.mrflags.
// Specifes additional information about an advertised / requested memory
// region needed by the NAA.
enum naaice_mrflags_value {

  // Indicates that memory region is for internal use on the NAA only, and
  // should not be written to or read from during data transfer.
  MRFLAG_INTERNAL = 0x01,

  // Indicates that memory region should be written back to the host during
  // memory transfer in addition to the return parameter memory region
  // specified in the metadata.
  // Does not override MRFLAG_INTERNAL; if both are set, the memory region
  // is not written back.
  MRFLAG_WRITEBACK = 0x02
};

// Struct to hold memory region request message.
struct naaice_mr_request
{
  uint64_t size;
};

// Struct to hold error message.
struct naaice_mr_error
{
  uint8_t code;
  uint8_t padding[2];
};

// Structs used as headers for the above messages.
// TODO: can these simply be included in the message structs?
struct naaice_mr_hdr
{
  uint8_t type;
};
struct naaice_mr_dynamic_hdr
{
  uint8_t count;
  uint8_t padding[2];
};

// Struct to hold information about a remote (i.e. peer) memory region.
struct naaice_mr_peer
{
  uint64_t addr;
  uint32_t rkey;
  size_t size;
};

// Struct to hold information about a local memory region.
struct naaice_mr_local
{
  struct ibv_mr *ibv;
  char *addr;
  size_t size;

  // Used by the software NAA implementation. Ignored in normal host-side
  // operation. If set, this memory region is always written back to the host
  // in addition to the return parameter memory region specified in the
  // metadata.
  bool writeback;
};

// Struct to hold information about a memory region used internally for
// calculation on the NAA.
// The addr field represents the MR's address in NAA memory space.
struct naaice_mr_internal
{
  char *addr;
  uint32_t size;
};

// Struct to hold metadata stored in a particular memory region,
// the metadata memory region.
// TODO: add support for routine-specific metadata fields.
struct naaice_rpc_metadata {
    uintptr_t return_addr;
};

// Connection state machine.
// Control flow:
//
// NAA goes from:
//  INIT: Starting state.
//  CONNECTED: Connection established.
//  MRSP_RECEIVING: Waiting for / processing MRSP packet from host.
//  MRSP_SENDING: Posting write for MRSP packet.
//  MRSP_DONE: Finished MRSP.
//  DATA_RECEIVING: Waiting for / processing data transfer from host.
//  CALCULATING: Running RPC.
//  DATA_SENDING: Posting write for data transfer back to host.
//  FINISHED: Done!
//
// Host goes from:
//  INIT: Starting state.
//  READY: Address resolved.
//  CONNECTED: Connection established.
//  MRSP_SENDING: Posting write for MRSP packet.
//  MRSP_REVEIVING: Waiting for / processing MRSP response from NAA.
//  MRSP_DONE: Finished MRSP.
//  DATA_SENDING: Posting write for data transfer to NAA.
//  DATA_RECEIVING: Waiting for / processing data transfer back from NAA.
//  FINISHED: Done!
enum naaice_communication_state
{
  INIT            = 00,
  READY           = 01,
  CONNECTED       = 02,
  MRSP_SENDING    = 10,
  MRSP_RECEIVING  = 11,
  MRSP_DONE       = 12,
  DATA_SENDING    = 20,
  CALCULATING     = 21,
  DATA_RECEIVING  = 22,
  FINISHED        = 30,
  ERROR           = 40,
};

// Struct which holds all information about the connection.
// Passed to almost all AP1 functions.
struct naaice_communication_context
{
  // Basic connection properties.
  struct rdma_cm_id *id;                  // Communication ID.
  struct rdma_event_channel *ev_channel;  // Event channel.
  struct ibv_context *ibv_ctx;            // IBV context.
  struct ibv_pd *pd;                      // Protection domain.
  struct ibv_comp_channel *comp_channel;  // Completion channel.
  struct ibv_cq *cq;                      // Completion queue.
  struct ibv_qp *qp;                      // Queue pair.

  // Current state.
  enum naaice_communication_state state;

  // Local memory regions.
  struct naaice_mr_local *mr_local_data;
  uint8_t no_local_mrs;

  // Index indicating which local memory region is the return region.
  // Set when the return address is set, in naaice_set_metadata.
  // Should be in range [1, no_local_mrs].
  uint8_t mr_return_idx;

  // Array of peer memory regions, i.e. information about memory regions of the
  // commuication partner.
  // Includes only symmetric memory regions, i.e. only MRs representing parameters
  // and not internal MRs used on the NAA just for computation.
  struct naaice_mr_peer *mr_peer_data;
  uint8_t no_peer_mrs;

  // Used for MRSP.
  struct naaice_mr_local *mr_local_message;

  // Array of internal memory regions, i.e. information about memory regions
  // on the NAA used only for computation which are not communicated during
  // data transfer.
  struct naaice_mr_internal *mr_internal;
  uint8_t no_internal_mrs;

  // Function code indicating which NAA routine to be called.
  uint8_t fncode;

  // Keeps track of number of writes done to NAA.
  uint8_t rdma_writes_done;
};


/* Public Functions **********************************************************/

/**
 * naaice_init_communication_context:
 *  Initializes communication context struct.
 *  After a call to this function, the provied communication context struct is
 *  ready to be passed to all other AP1 functions.
 * 
 * params:
 *  naaice_communication_context **comm_ctx: (return param)
 *    Double pointer to communication context struct to be initialized.
 *    Should not point to an existing struct; the struct is allocated
 *    and returned by this function.
 *  unsigned int *param_sizes:
 *    Array of sizes (in bytes) of the provided routine parameters.
 *  char **params:
 *    Array of pointers to parameter data. Should be preallocated by
 *    the host application.
 *  unsigned int params_amount:
 *    Number of params. Used to index param_sizes and params, so their lengths
 *    should correspond to params_amount.
 *  uint8_t fncode:
 *    Function code specifying which NAA routine to be called.
 *  const char *remote_ip:
 *    String specifying remote address, ex. "10.3.10.135".
 *  uint16_t port:
 *    Value specifying connection port, ex. 12345.
 * 
 * returns:
 *  0 if sucessful, -1 if not.
 */
int naaice_init_communication_context(
  struct naaice_communication_context **comm_ctx,
  unsigned int *param_sizes, char **params, unsigned int params_amount,
  uint8_t fncode, const char *remote_ip, uint16_t port);

/**
 * naaice_poll_connection_event:
 *  Polls for a connection event on the RDMA event channel stored in the
 *  communication context. If a connection event is recieved, stores it
 *  in the provided event pointer.
 * 
 * params:
 *  naaice_communication_context *comm_ctx:
 *    Pointer to struct describing the connection to be polled.
 *  struct rdma_cm_event ev: (return param)
 *    Pointer to event recieved.
 * 
 * returns:
 *  0 if sucessful (regardless of whether an event is recieved), -1 if not.
 */
int naaice_poll_connection_event(struct naaice_communication_context *comm_ctx,
                                 struct rdma_cm_event *ev,
                                 struct rdma_cm_event *ev_cp);

/**
 * connection event handlers:
 *  These functions each handle a specific connection event. If the provided
 *  event's type matches the event type of the handler function, it executes
 *  the necessary logic to handle it.
 * 
 *  Subsequently, flags in the communication context are updated to represent
 *  the current status of connection establishment.
 * 
 *  The events handled by these functions are, in order:
 *  RDMA_CM_EVENT_ADDR_RESOLVED
 *  RDMA_CM_EVENT_ROUTE_RESOLVED
 *  RDMA_CM_EVENT_CONNECT_REQUEST
 *  RDMA_CM_EVENT_CONNECT_ESTABLISHED
 * 
 *  Also, the following are handled by naaice_handle_error:
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
int naaice_handle_addr_resolved(
  struct naaice_communication_context *comm_ctx,
  struct rdma_cm_event *ev);
int naaice_handle_route_resolved(
  struct naaice_communication_context *comm_ctx,
  struct rdma_cm_event *ev);
int naaice_handle_connection_requests(
  struct naaice_communication_context *comm_ctx,
  struct rdma_cm_event *ev);
int naaice_handle_connection_established(
  struct naaice_communication_context *comm_ctx,
  struct rdma_cm_event *ev);
int naaice_handle_error(
  struct naaice_communication_context *comm_ctx,
  struct rdma_cm_event *ev);
int naaice_handle_other(
  struct naaice_communication_context *comm_ctx, 
  struct rdma_cm_event *ev);

/**
 * naaice_poll_and_handle_connection_event:
 *  Polls for a connection event on the RDMA event channel stored in the
 *  communication context and handles the event if one is recieved.
 *  Simply uses the poll and handle functions above.
 * 
 * params:
 *  naaice_communication_context *comm_ctx:
 *    Pointer to struct describing the connection.
 * 
 * returns:
 *  0 if sucessful (regardless of whether an event is recieved), -1 if not.
 */
int naaice_poll_and_handle_connection_event(
  struct naaice_communication_context *comm_ctx);

/**
 * naaice_setup_connection:
 *  Loops polling for and handling connection events until connection setup
 *  is complete. Simply uses naaice_poll_and_handle_connection_event.
 * 
 *  TODO: confirm that stopping condition is correct.
 *  TODO: implement timeout.
 * 
 * params:
 *  naaice_communication_context *comm_ctx:
 *    Pointer to struct describing the connection.
 * 
 * returns:
 *  0 if sucessful, -1 if not (due to timeout).
 */
int naaice_setup_connection(struct naaice_communication_context *comm_ctx);

/**
 * naaice_register_mrs:
 *  Registers local memory regions as IBV memory regions using ibv_reg_mr.
 *  This includes memory regions corresponding to input and output params,
 *  the single metadata memory region, and the single memory region used
 *  for MRSP.
 *  If an error occurs, the remote peer is notified via naaice_send_message.
 * 
 * params:
 *  naaice_communication_context *comm_ctx:
 *    Pointer to struct describing the connection and memory regions.
 * 
 * returns:
 *  0 if sucessful, -1 if not.
 */
int naaice_register_mrs(struct naaice_communication_context *comm_ctx);

/**
 * naaice_set_metadata:
 *  Sets the fields of the metadata memory region.
 *  Specifically, this sets the return_addr field, which specifies which
 *  memory region the NAA should write results back to.
 * 
 *  Should only be called once. Each call to this function overwrites the
 *  previous call.
 * 
 *  TODO: additionally handle routine-specific metadata fields.
 * 
 * params:
 *  naaice_communication_context *comm_ctx:
 *    Pointer to struct describing the connection.
 *  uintptr_t return_addr:
 *    Address of memory region to be used as the return param for the RPC.
 * 
 * returns:
 *  0 if sucessful, -1 if not.
 */
int naaice_set_metadata(struct naaice_communication_context *comm_ctx,
  uintptr_t return_addr);

/**
 * naaice_set_internal_mrs
 *  Adds information about internal memory regions to the communication
 *  context. Such memory regions exist only on the NAA side and are used for
 *  computation. The contents of these memory region are not communicated
 *  during data transfer.
 * 
 *  Must be called before naaice_init_mrsp. The internal memory regions will
 *  then be included in the memory region announcement message, indicating that
 *  they should be allocated by the NAA.
 * 
 *  Should only be called once. Each call to this function overwrites the
 *  previous call.
 * 
 * params:
 *  naaice_communication_context *comm_ctx:
 *    Pointer to struct describing the connection.
 *  unsigned int n_internal_mrs:
 *    Number of internal memory regions.
 *  uintptr_t *addrs:
 *    Array of addresses of the internal memory regions in NAA memory space.
 *    These addresses will be requested of the NAA during MRSP.
 *  uint32_t *sizes:
 *    Sizes of the internal memory region, in bytes.
 * 
 * returns:
 *  0 if sucessful, -1 if not.
 */
int naaice_set_internal_mrs(struct naaice_communication_context *comm_ctx,
  unsigned int n_internal_mrs, uintptr_t *addrs, size_t *sizes);

/**
 * naaice_init_mrsp:
 *  Starts the MRSP. That is, sends advertise/request packets and posts a
 *  recieve for the response.
 * 
 * params:
 *  naaice_communication_context *comm_ctx:
 *    Pointer to struct describing the connection and memory regions.
 * 
 * returns:
 *  0 if sucessful, -1 if not.
 */
int naaice_init_mrsp(struct naaice_communication_context *comm_ctx);

/**
 * naaice_init_data_transfer:
 *  Starts the data transfer. That is, posts the write for memory regions to
 *  the NAA.
 * 
 * params:
 *  naaice_communication_context *comm_ctx:
 *    Pointer to struct describing the connection and memory regions.
 * 
 * returns:
 *  0 if sucessful, -1 if not.
 */
int naaice_init_data_transfer(struct naaice_communication_context *comm_ctx);

/**
 * naaice_handle_work_completion:
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
int naaice_handle_work_completion(struct ibv_wc *wc,
  struct naaice_communication_context *comm_ctx);

/**
 * naaice_poll_cq_nonblocking:
 *  Polls the completion queue for any work completions, and handles them if
 *  any are recieved using naaice_handle_work_completion.
 *  
 *  Subsequently, comm_ctx->state is updated to reflect the current state
 *  of the NAA connection and routine.
 * 
 * params:
 *  naaice_communication_context *comm_ctx:
 *    Pointer to struct describing the connection.
 * 
 * returns:
 *  0 if sucessful (regardless of whether any work completions are recieved),
 * -1 if not.
 */
int naaice_poll_cq_nonblocking(struct naaice_communication_context *comm_ctx);

/**
 * naaice_disconnect_and_cleanup:
 *  Terminates the connection and frees all communication context memory.
 * 
 * params:
 *  naaice_communication_context *comm_ctx:
 *    Pointer to struct describing the connection.
 * 
 * returns:
 *  0 if sucessful, -1 if not.
 */
int naaice_disconnect_and_cleanup(
  struct naaice_communication_context *comm_ctx);

/**
 * naaice_send_message:
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
int naaice_send_message(struct naaice_communication_context *comm_ctx,
  enum message_id message_type, uint8_t errorcode);

/**
 * naaice_write_data:
 *  Writes memory regions (metadata and input parameters) to the NAA. Done
 *  with a ibv_post_send using opcode IBV_WR_RDMA_WRITE or
 *  IBV_WR_RDMA_WRITE_WITH_IMM (for the final memory region).
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
int naaice_write_data(struct naaice_communication_context *comm_ctx,
  uint8_t fncode);

/**
 * naaice_post_recv_mrsp
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
int naaice_post_recv_mrsp(struct naaice_communication_context *comm_ctx);

/**
 * naaice_post_recv_data
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
int naaice_post_recv_data(struct naaice_communication_context *comm_ctx);

/**
 * naaice_init_rdma_resources
 *  Allocates a protection domain, completion channel, completion queue, and
 *  queue pair.
 *
 * params:
 *  naaice_communication_context *comm_ctx:
 *    Pointer to struct describing the connection.
 *
 * returns:
 *  0 if successful, -1 if not.
 */
int naaice_init_rdma_resources(struct naaice_communication_context *comm_ctx);

/**
 * naaice_do_mrsp
 *  Does all logic for the MRSP in a blocking fashion.
 *
 * params:
 *  naaice_communication_context *comm_ctx:
 *    Pointer to struct describing the connection.
 *
 * returns:
 *  0 if successful, -1 if not.
 */
int naaice_do_mrsp(struct naaice_communication_context *comm_ctx);

/**
 * naaice_do_data_transfer
 *  Does all logic for the data transfer, including writing data to the NAA,
 *  wating for the NAA calculation, and receiving the return data back, in a
 *  blocking fashion.
 *
 * params:
 *  naaice_communication_context *comm_ctx:
 *    Pointer to struct describing the connection.
 *
 * returns:
 *  0 if successful, -1 if not.
 */
int naaice_do_data_transfer(struct naaice_communication_context *comm_ctx);

#endif