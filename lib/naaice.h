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
 * 26-01-2024
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
 *    Aligned memory isn't strictly necessary, it just has some performance
 * implications
 *
 *
 * - Handle errors better, utilizing errno and ERROR state in state machine.
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
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <unistd.h>

#include <limits.h>
#include <netdb.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include <sys/socket.h>

/* Constants *****************************************************************/

#define RX_DEPTH 1025
#define ntohll be64toh
#define htonll htobe64

/** @defgroup defines Default defines */
/** @defgroup StructsEnums Structs & Enums */

/** @ingroup defines
 * @brief Timeout for resolving RDMA routes, in milliseconds. */
#define TIMEOUT_RESOLVE_ROUTE 500

/** @ingroup defines
 * @brief Timeout for poll functions, in milliseconds. */
#define POLLING_TIMEOUT 500

/** @ingroup defines
 * @brief Default timeout for blocking calls to the NAA, in seconds.
 */
#define DEFAULT_TIMEOUT 3600

/** @ingroup defines Default number of retries for the RDMA connection. */
#define DEFAULT_RETRY_COUNT 7

#ifdef __cplusplus
extern "C" {
#endif

// Maximum allowed number of memory regions.
// Total of parameters and internal NAA memory regions.
#define MAX_MRS 32

// Immediate return code offset
#define IMMEDIATE_OFFSET 8

// size of MR for MR exchange
// type + padding: 32 + Maximum number of regions:UINT_MAX+1
//  * (Addr:64 +  size:32 + key:32) + requested size:64 / 8
// TODO: rewrite in terms of struct sizes.
// #define MR_SIZE_MR_EXHANGE (32 + (UINT8_MAX + 1) * 2 * 64 + 64) / 8

// Size of memory region used for MRSP.
// Simply the size of a memory region advertisement and request message
// multiplied by the maximum allowed number of memory regions.
// TODO: add header size.
#define MR_SIZE_MRSP sizeof(struct naaice_mr_advertisement_request) * MAX_MRS

// FPGA addresses are sent in a 7-byte region, so this is the maximum value
// the can take (2^56-1)

/* Structs and Enums *********************************************************/

/**
 * @brief Message IDs used for packets sent between host and NAA.
 *
 * Defines the types of messages exchanged during the Memory Region
 * Setup Protocol (MRSP) and other control communication.
 */
enum message_id {
  /// Error message.
  MSG_MR_ERR = 0x00,

  /// Memory region advertisement and request, sent by host.
  MSG_MR_AAR = 0x01,

  /// Memory region advertisement without request, usually sent by NAA.
  MSG_MR_A = 0x02,

  /// Reserved/unused message ID.
  MSG_MR_R = 0x03,
};

/**
 * @brief Memory region advertisement message structure.
 *
 * Holds information about a memory region that is advertised to a
 * remote peer, including its address, remote key, and size. Used
 * in the Memory Region Setup Protocol (MRSP).
 */
struct naaice_mr_advertisement {
  /// Address of the memory region.
  uint64_t addr;

  /// Remote key (rkey) for RDMA access.
  uint32_t rkey;

  /// Size of the memory region, in bytes.
  uint32_t size;
};

/**
 * @brief Memory region advertisement and request message structure.
 *
 * Holds information about a memory region that is both advertised and
 * requested. This includes memory region flags, the requested NAA
 * address, the RDMA remote key, and the size of the region.
 *
 * @note TODO: Could refactor to include a ::naaice_mr_advertisement
 *       struct as a member.
 */
struct naaice_mr_advertisement_request {
  union {
    /// Packed memory region info (flags + requested NAA address).
    uint64_t mr_info;

    /// Byte array representation of memory region info.
    /// The first (0th) byte holds MR flags, the remaining 7 bytes hold
    /// the requested NAA MR address.
    uint8_t mr_info_bytearray[8];
  };

  /// Requested NAA memory region address.
  uint64_t addr;

  /// Remote key (rkey) for RDMA access.
  uint32_t rkey;

  /// Size of the memory region, in bytes.
  uint32_t size;
};

/**
 * @brief Flags for memory regions in ::naaice_mr_advertisement_request.
 *
 * Specifies additional information about an advertised or requested
 * memory region needed by the NAA.
 */
enum naaice_mrflags_value {

  /// Memory region is for internal use on the NAA only; not transferred.
  MRFLAG_INTERNAL = 0x01,

  /// Memory region is sent only once on the first RPC; subsequent RPCs skip it.
  MRFLAG_SINGLESEND = 0x02,

  /// Memory region represents an RPC input parameter.
  MRFLAG_INPUT = 0x04,

  /// Memory region represents an RPC output parameter.
  MRFLAG_OUTPUT = 0x08,
};

/**
 * @brief Memory region request message structure.
 *
 * Represents a request for a memory region from the NAA, specifying
 * the size of the region to be allocated.
 */
struct naaice_mr_request {
  /// Size of the requested memory region, in bytes.
  uint64_t size;
};

/**
 * @brief Memory region error message structure.
 *
 * Represents an error message sent between host and NAA during the
 * Memory Region Setup Protocol (MRSP).
 */
struct naaice_mr_error {
  /// Error code indicating the type of error.
  uint8_t code;

  /// Padding bytes for alignment.
  uint8_t padding[2];
};

// Structs used as headers for the above messages.
// TODO: can these simply be included in the message structs?
struct naaice_mr_hdr {
  uint8_t type;
};

struct naaice_mr_dynamic_hdr {
  uint8_t count;
  uint8_t padding[2];
};

/**
 * @brief Information about a remote (peer) memory region.
 *
 * Holds metadata for a memory region located on the remote NAA,
 * including its address, remote key, size, and transfer behavior flags.
 */
struct naaice_mr_peer {
  union {
    /// Address of the remote memory region.
    uint64_t addr;

    /// Byte array representation of the remote address (e.g., FPGA address).
    uint8_t fpgaaddr[8];
  };

  /// Remote key (rkey) for RDMA access.
  uint32_t rkey;

  /// Size of the memory region, in bytes.
  size_t size;

  /// Indicates that this memory region should be written back from the NAA.
  bool to_write;

  /// Indicates that this memory region is a single-send region (written only
  /// once).
  bool single_send;
};

/**
 * @brief Information about a local memory region.
 *
 * Holds metadata for a memory region located on the host, including
 * the registered IBV memory region, local address, size, and transfer
 * behavior flags.
 */
struct naaice_mr_local {
  /// Pointer to the registered IBV memory region.
  struct ibv_mr *ibv;

  /// Local address of the memory region in user space.
  char *addr;

  /// Size of the memory region, in bytes.
  size_t size;

  /// Indicates that this memory region should be written to the remote NAA.
  bool to_write;

  /// Indicates that this memory region is a single-send region (written only
  /// once).
  bool single_send;
};

/**
 * @brief Information about an internal memory region on the NAA.
 *
 * Represents a memory region used internally for computation on the NAA.
 * These regions exist only on the NAA side and are not transferred during
 * data transfer.
 */
struct naaice_mr_internal {
  union {
    /// Address of the internal memory region in NAA memory space.
    uint64_t addr;

    /// Byte array representation of the internal address (e.g., FPGA address).
    uint8_t fpgaaddr[8];
  };

  /// Size of the internal memory region, in bytes.
  size_t size;
};

/**
 * @brief Metadata stored in a dedicated memory region for RPC.
 *
 * Contains information about a particular RPC, including the address
 * where the return data should be written.
 */
struct naaice_rpc_metadata {
  /// Address in memory where the RPC return value should be written.
  uintptr_t return_addr;
};

/**
 * @ingroup StructsEnums
 * @brief Connection state machine for NAA communication.
 *
 * Defines the possible states of a communication session between the
 * host and NAA. States represent the progression from connection
 * establishment, through MRSP and data transfer, to completion.
 *
 * @note The state flow differs slightly between the host and the NAA.
 *
 * Host states:
 * - NAAICE_INIT: Starting state.
 * - NAAICE_READY: Address resolved.
 * - NAAICE_CONNECTED: Connection established.
 * - NAAICE_MRSP_SENDING: Posting write for MRSP packet.
 * - NAAICE_MRSP_RECEIVING: Waiting for / processing MRSP response from NAA.
 * - NAAICE_MRSP_DONE: Finished MRSP.
 * - NAAICE_DATA_SENDING: Posting write for data transfer to NAA.
 * - NAAICE_DATA_RECEIVING: Waiting for / processing data transfer back from
 * NAA.
 * - NAAICE_FINISHED: Done!
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
 * - NAAICE_FINISHED: Done!
 */
enum naaice_communication_state {
  NAAICE_INIT = 0,            ///< Starting state
  NAAICE_READY = 1,           ///< Address resolved (host only)
  NAAICE_CONNECTED = 2,       ///< Connection established
  NAAICE_DISCONNECTED = 3,    ///< Connection disconnected
  NAAICE_MRSP_SENDING = 10,   ///< Posting write for MRSP packet
  NAAICE_MRSP_RECEIVING = 11, ///< Waiting for / processing MRSP packet
  NAAICE_MRSP_DONE = 12,      ///< Finished MRSP
  NAAICE_DATA_SENDING = 20,   ///< Posting write for data transfer
  NAAICE_CALCULATING = 21,    ///< Running RPC on NAA
  NAAICE_DATA_RECEIVING = 22, ///< Waiting for / processing data transfer
  NAAICE_FINISHED = 30,       ///< Completed all communication
  NAAICE_ERROR = 40,          ///< Error state
};

/**
 * @brief Communication context for NAA connections.
 *
 * Holds all information about a connection, including RDMA resources,
 * memory regions, current state, function codes, and transfer metadata.
 * This struct is passed to almost all AP1 functions.
 */
struct naaice_communication_context {
  /* --- Basic connection properties --- */

  /// RDMA communication identifier.
  struct rdma_cm_id *id;

  /// RDMA event channel.
  struct rdma_event_channel *ev_channel;

  /// IBV device context.
  struct ibv_context *ibv_ctx;

  /// Protection domain for memory regions.
  struct ibv_pd *pd;

  /// Completion channel.
  struct ibv_comp_channel *comp_channel;

  /// Completion queue.
  struct ibv_cq *cq;

  /// Queue pair.
  struct ibv_qp *qp;

  /// Timeout for operations, in seconds.
  double timeout;

  /// Retry count for RDMA connection.
  uint8_t retry_count;

  /* --- Current connection state --- */

  /// Current communication state.
  enum naaice_communication_state state;

  /* --- Local memory regions --- */

  /// Array of local memory regions.
  struct naaice_mr_local *mr_local_data;

  /// Number of local memory regions.
  uint8_t no_local_mrs;

  /// Index of the return memory region (1..no_local_mrs), set by
  /// ::naaice_set_metadata.
  uint8_t mr_return_idx;

  /* --- Peer memory regions --- */

  /// Array of peer memory regions representing symmetric parameters.
  struct naaice_mr_peer *mr_peer_data;

  /// Number of peer memory regions.
  uint8_t no_peer_mrs;

  /* --- MRSP-related memory --- */

  /// Local memory region used for MRSP messages.
  struct naaice_mr_local *mr_local_message;

  /* --- Internal memory regions on NAA --- */

  /// Array of internal memory regions on NAA used for computation.
  struct naaice_mr_internal *mr_internal;

  /// Number of internal memory regions.
  uint8_t no_internal_mrs;

  /* --- Function and return codes --- */

  /// Function code specifying which NAA routine to call.
  uint8_t fncode;

  /// Return code indicating success or failure of NAA routine.
  uint8_t naa_returncode;

  /* --- Transfer tracking --- */

  /// Number of RDMA writes performed to NAA.
  uint8_t rdma_writes_done;

  /// Number of bytes received from NAA.
  uint32_t bytes_received;

  /// Number of input memory regions (parameters).
  uint8_t no_input_mrs;

  /// Number of output memory regions (parameters).
  uint8_t no_output_mrs;

  /// Number of RPC calls performed on this connection.
  unsigned int no_rpc_calls;

  /* --- Immediate value for RDMA transfers --- */

  /**
   * 32-bit immediate value sent during RDMA transfers.
   * - First byte holds function code (set with
   * ::naaice_init_communication_context)
   * - Remaining bytes can be set using ::naaice_set_immediate
   */
  union {
    uint32_t immediate;
    uint8_t immediate_bytearr[4];
  };
};

/* Public Functions **********************************************************/

/**
 * @defgroup PublicFunctions Functions
 */

/** @addtogroup PublicFunctions
 *  @{
 */

/**
 * @brief Initializes communication context struct.
 *
 * After a call to this function, the provided communication context struct is
 * ready to be passed to all other API functions.
 *
 * @param[out] comm_ctx
 *   Double pointer to communication context struct to be initialized.
 *   Must not point to an existing struct; the struct is allocated
 *   and returned by this function.
 *
 * @param param_sizes
 *   Array of sizes (in bytes) of the provided routine parameters.
 *
 * @param params
 *   Array of pointers to parameter data. Must be preallocated by
 *   the host application.
 *
 * @param params_amount
 *   Number of parameters. Used to index @p param_sizes and @p params.
 *
 * @param internal_mr_amount
 *   Number of internal memory regions. Used to index
 *   @p internal_mr_sizes.
 *
 * @param internal_mr_sizes
 *   Array of sizes (in bytes) of the internal memory regions.
 *
 * @param fncode
 *   Function code specifying which NAA routine is called.
 *
 * @param local_address
 *   Local address string (e.g. "10.3.10.136").
 *   Optional; if NULL, it is not used.
 *   Do not provide a local address when running in loopback mode.
 *
 * @param remote_address
 *   Remote address string (e.g. "10.3.10.135").
 *
 * @param port
 *   Connection port. A value of 0 enables dynamic port allocation.
 *
 * @return
 *   0 on success, -1 on failure.
 */
int naaice_init_communication_context(
    struct naaice_communication_context **comm_ctx, uint64_t addr_offset,
    size_t *param_sizes, char **params, unsigned int params_amount,
    unsigned int internal_mr_amount, size_t *internal_mr_sizes, uint8_t fncode,
    const char *local_address, const char *remote_address, uint16_t port);

/**
 * @brief Polls for a connection event on the RDMA event channel.
 *
 * Polls the RDMA event channel stored in the communication context.
 * If a connection event is received, it is stored in the provided
 * event pointer.
 *
 * @param comm_ctx
 *   Pointer to the communication context describing the connection
 *   to be polled.
 *
 * @param[out] ev
 *   Pointer to the received RDMA connection event.
 *
 * @return
 *   0 on success (regardless of whether an event was received),
 *   -1 on failure.
 */

int naaice_poll_connection_event(struct naaice_communication_context *comm_ctx,
                                 struct rdma_cm_event *ev,
                                 struct rdma_cm_event *ev_cp);

/**
 * @defgroup connection_event_handlers Connection event handlers
 * @brief Handlers for RDMA connection management events.
 *
 * These functions each handle a specific RDMA connection event. If the
 * provided event's type matches the event type of the handler function,
 * the corresponding logic is executed.
 *
 * After handling an event, flags in the communication context are updated
 * to reflect the current state of connection establishment.
 *
 * The events handled by these functions, in order, are:
 * - RDMA_CM_EVENT_ADDR_RESOLVED
 * - RDMA_CM_EVENT_ROUTE_RESOLVED
 * - RDMA_CM_EVENT_CONNECT_ESTABLISHED
 *
 * The following events are handled by ::naaice_handle_error:
 * - RDMA_CM_EVENT_ADDR_ERROR
 * - RDMA_CM_EVENT_ROUTE_ERROR
 * - RDMA_CM_EVENT_CONNECT_ERROR
 * - RDMA_CM_EVENT_UNREACHABLE
 * - RDMA_CM_EVENT_REJECTED
 * - RDMA_CM_EVENT_DEVICE_REMOVAL
 * - RDMA_CM_EVENT_DISCONNECTED
 *
 * @param comm_ctx
 *   Pointer to the communication context describing the connection
 *   whose events are handled.
 *
 * @param ev
 *   Pointer to the RDMA connection management event to be checked and
 *   possibly handled.
 *
 * @return
 *   0 on success (either the event was handled successfully or it did not
 *   match the handler’s event type), -1 on failure.
 * @{
 */

/**
 * @brief Handle RDMA_CM_EVENT_ADDR_RESOLVED.
 */
int naaice_handle_addr_resolved(struct naaice_communication_context *comm_ctx,
                                struct rdma_cm_event *ev);

/**
 * @brief Handle RDMA_CM_EVENT_ROUTE_RESOLVED.
 */
int naaice_handle_route_resolved(struct naaice_communication_context *comm_ctx,
                                 struct rdma_cm_event *ev);

/**
 * @brief Handle RDMA_CM_EVENT_CONNECT_ESTABLISHED.
 */
int naaice_handle_connection_established(
    struct naaice_communication_context *comm_ctx, struct rdma_cm_event *ev);

/**
 * @brief Handle RDMA connection error events.
 *
 * Handles RDMA_CM_EVENT_ADDR_ERROR, RDMA_CM_EVENT_ROUTE_ERROR,
 * RDMA_CM_EVENT_CONNECT_ERROR, RDMA_CM_EVENT_UNREACHABLE,
 * RDMA_CM_EVENT_REJECTED, RDMA_CM_EVENT_DEVICE_REMOVAL and
 * RDMA_CM_EVENT_DISCONNECTED.
 */
int naaice_handle_error(struct naaice_communication_context *comm_ctx,
                        struct rdma_cm_event *ev);

/**
 * @brief Handle all other, unsupported or unexpected RDMA CM events.
 */
int naaice_handle_other(struct naaice_communication_context *comm_ctx,
                        struct rdma_cm_event *ev);

/** @} */

/**
 * @brief Poll and handle a connection event on the RDMA event channel.
 *
 * Polls the RDMA event channel stored in the communication context and,
 * if an event is received, dispatches it to the appropriate connection
 * event handler. This function is a convenience wrapper that combines
 * polling and handling using the previously defined poll and handler
 * functions.
 *
 * @param comm_ctx
 *   Pointer to the communication context describing the connection.
 *
 * @return
 *   0 on success (regardless of whether an event was received),
 *   -1 on failure.
 */

int naaice_poll_and_handle_connection_event(
    struct naaice_communication_context *comm_ctx);

/**
 * @brief Set up the RDMA connection.
 *
 * Repeatedly polls for and handles RDMA connection events until the
 * connection setup is complete. This function is a blocking helper that
 * internally calls ::naaice_poll_and_handle_connection_event.
 *
 *
 * @param comm_ctx
 *   Pointer to the communication context describing the connection.
 *
 * @return
 *   0 on success, -1 on failure (e.g. due to timeout).
 */
int naaice_setup_connection(struct naaice_communication_context *comm_ctx);

/**
 * @brief Register local memory regions.
 *
 * Registers local memory regions as IBV memory regions using
 * ::ibv_reg_mr. This includes memory regions corresponding to input
 * and output parameters, the single metadata memory region, and the
 * memory region used for MRSP.
 *
 * If an error occurs during registration, the remote peer is notified
 * via ::naaice_send_message.
 *
 * @param comm_ctx
 *   Pointer to the communication context describing the connection and
 *   associated memory regions.
 *
 * @return
 *   0 on success, -1 on failure.
 */
int naaice_register_mrs(struct naaice_communication_context *comm_ctx);

/**
 * @brief Allocate and initialize parameter memory region descriptors.
 *
 * Allocates and initializes internal structures for parameter (i.e.
 * non-internal) memory regions. This function is called from
 * ::naaice_init_communication_context.
 *
 * @param comm_ctx
 *   Pointer to the communication context describing the connection and
 *   memory regions.
 *
 * @param n_parameter_mrs
 *   Number of parameter memory regions for which descriptors will be
 *   allocated.
 *
 * @param local_addrs
 *   Array of local (user-space) addresses of the memory regions.
 *   This should correspond to the @p params array passed to
 *   ::naaice_init_communication_context.
 *
 * @param remote_addrs
 *   Array of remote addresses in NAA memory where the memory regions
 *   are requested to be stored. These addresses are obtained from the
 *   memory management service.
 *
 * @param sizes
 *   Array of sizes (in bytes) of the memory regions. This should
 *   correspond to the @p param_sizes array passed to
 *   ::naaice_init_communication_context.
 *
 * @return
 *   0 on success, -1 on failure.
 */
int naaice_set_parameter_mrs(struct naaice_communication_context *comm_ctx,
                             unsigned int n_parameter_mrs,
                             uint64_t *local_addrs, uint64_t *remote_addrs,
                             size_t *sizes);

/**
 * @brief Mark a memory region as input and/or output.
 *
 * Adds information to the communication context indicating whether a
 * memory region contains an input parameter, an output parameter, or
 * both. Input memory regions are written to the NAA during data transfer,
 * while output memory regions are written back from the NAA.
 *
 * This function must be called before ::naaice_init_mrsp.
 *
 * @param comm_ctx
 *   Pointer to the communication context describing the connection.
 *
 * @param idx
 *   Index of the memory region to be marked as input or output.
 *
 * @return
 *   0 on success, -1 on failure.
 */

/**
 * @brief Mark a memory region as an input parameter.
 */
int naaice_set_input_mr(struct naaice_communication_context *comm_ctx,
                        unsigned int input_mr_idx);

/**
 * @brief Mark a memory region as an output parameter.
 */
int naaice_set_output_mr(struct naaice_communication_context *comm_ctx,
                         unsigned int output_mr_idx);

/**
 * @brief Mark a memory region as a single-send region.
 *
 * Adds information to the communication context indicating that a memory
 * region is a single-send region. Single-send regions are written exactly
 * once during the first RPC on a connection and are not written again
 * during subsequent RPCs.
 *
 * If the memory region is not already marked as an input region, it is
 * automatically marked as one.
 *
 * This function must be called before ::naaice_init_mrsp.
 *
 * @param comm_ctx
 *   Pointer to the communication context describing the connection.
 *
 * @param idx
 *   Index of the memory region to be marked as a single-send region.
 *
 * @return
 *   0 on success, -1 on failure.
 */

int naaice_set_singlesend_mr(struct naaice_communication_context *comm_ctx,
                             unsigned int singlesend_mr_idx);

/**
 * @brief Add internal memory regions to the communication context.
 *
 * Adds information about internal memory regions to the communication
 * context. Internal memory regions exist only on the NAA side and are
 * used for computation; their contents are not communicated during data
 * transfer.
 *
 * This function must be called before ::naaice_init_mrsp. The internal
 * memory regions will be included in the memory region announcement
 * message, indicating that they should be allocated by the NAA.
 *
 * Should only be called once per communication context. Each call
 * overwrites any previous internal memory region information.
 *
 * @param comm_ctx
 *   Pointer to the communication context describing the connection.
 *
 * @param n_internal_mrs
 *   Number of internal memory regions.
 *
 * @param addrs
 *   Array of addresses of the internal memory regions in NAA memory
 *   space. These addresses will be requested of the NAA during MRSP.
 *
 * @param sizes
 *   Array of sizes (in bytes) of the internal memory regions.
 *
 * @return
 *   0 on success, -1 on failure.
 */
int naaice_set_internal_mrs(struct naaice_communication_context *comm_ctx,
                            unsigned int n_internal_mrs, uint64_t *addrs,
                            size_t *sizes);

/**
 * @brief Set the immediate value for data transfer.
 *
 * Sets the immediate value to be written during data transfer. The
 * immediate consists of up to 3 user-specified bytes placed in the
 * upper 3 bytes; the lowest byte is reserved for the function code.
 *
 * This function must be called before ::naaice_init_data_transfer.
 *
 * @param comm_ctx
 *   Pointer to the communication context describing the connection.
 *
 * @param imm_bytes
 *   Array of immediate value bytes. Maximum of 3 bytes.
 *
 * @return
 *   0 on success, -1 on failure.
 */

int naaice_set_immediate(struct naaice_communication_context *comm_ctx,
                         uint8_t *imm_bytes);

/**
 * @brief Initialize the Memory Region Setup Protocol (MRSP).
 *
 * Starts the MRSP by sending advertise/request packets and posting a
 * receive for the response. This prepares the connection for memory
 * region registration and data transfer.
 *
 * @param comm_ctx
 *   Pointer to the communication context describing the connection and
 *   associated memory regions.
 *
 * @return
 *   0 on success, -1 on failure.
 */

int naaice_init_mrsp(struct naaice_communication_context *comm_ctx);

/**
 * @brief Start the data transfer to the NAA.
 *
 * Initiates the data transfer by posting write operations for the
 * memory regions to the NAA. Prepares the connection for remote
 * computation using the previously registered memory regions.
 *
 * @param comm_ctx
 *   Pointer to the communication context describing the connection and
 *   associated memory regions.
 *
 * @return
 *   0 on success, -1 on failure.
 */

int naaice_init_data_transfer(struct naaice_communication_context *comm_ctx);

/**
 * @brief Handle a single work completion from the completion queue.
 *
 * Processes a single work completion event from the completion queue,
 * which typically represents a memory region write either from the
 * host to the NAA or from the NAA to the host.
 *
 * @param comm_ctx
 *   Pointer to the communication context describing the connection.
 *
 * @return
 *   0 on success, -1 on failure.
 */
int naaice_handle_work_completion(
    struct ibv_wc *wc, struct naaice_communication_context *comm_ctx);

/**
 * @brief Poll the completion queue non-blocking.
 *
 * Polls the completion queue for any work completions and processes them
 * using ::naaice_handle_work_completion if any are received. After
 * handling, updates ::comm_ctx->state to reflect the current state of
 * the NAA connection and routine.
 *
 * @param comm_ctx
 *   Pointer to the communication context describing the connection.
 *
 * @return
 *   0 on success (regardless of whether any work completions were received),
 *  -1 on failure.
 */
int naaice_poll_cq_nonblocking(struct naaice_communication_context *comm_ctx);

/**
 * @brief Poll the completion queue blocking.
 *
 * Polls the completion queue for a work completion and blocks until at
 * least one completion is available. Once a completion is received, it
 * is processed using ::naaice_handle_work_completion. After handling,
 * ::comm_ctx->state is updated to reflect the current state of the NAA
 * connection and routine.
 *
 * @param comm_ctx
 *   Pointer to the communication context describing the connection.
 *
 * @return
 *   0 on success (regardless of whether any work completions were received),
 *  -1 on failure.
 */

int naaice_poll_cq_blocking(struct naaice_communication_context *comm_ctx);

/**
 * @brief Disconnect and clean up the communication context.
 *
 * Terminates the RDMA connection and frees all memory associated with
 * the communication context.
 *
 * @param comm_ctx
 *   Pointer to the communication context describing the connection.
 *
 * @return
 *   0 on success, -1 on failure.
 */
int naaice_disconnect_and_cleanup(
    struct naaice_communication_context *comm_ctx);

/**
 * @brief Send an MRSP message to the remote peer.
 *
 * Sends an MRSP packet to the remote peer using ::ibv_post_send with
 * opcode ::IBV_WR_SEND.
 *
 * @param comm_ctx
 *   Pointer to the communication context describing the connection.
 *
 * @param message_type
 *   Type of message to send. Should be one of ::MSG_MR_ERR,
 *   ::MSG_MR_AAR, or ::MSG_MR_A.
 *
 * @param errorcode
 *   Error code to include in the packet if @p message_type is ::MSG_MR_ERR.
 *   Ignored for other message types.
 *
 * @return
 *   0 on success, -1 on failure.
 */
int naaice_send_message(struct naaice_communication_context *comm_ctx,
                        enum message_id message_type, uint8_t errorcode);

/**
 * @brief Write memory regions to the NAA.
 *
 * Writes memory regions, including metadata and input parameters, to the
 * NAA. Uses ::ibv_post_send with opcode ::IBV_WR_RDMA_WRITE for regular
 * regions or ::IBV_WR_RDMA_WRITE_WITH_IMM for the final memory region.
 *
 * @param comm_ctx
 *   Pointer to the communication context describing the connection.
 *
 * @param fncode
 *   Function code for the NAA routine. Must be positive; 0 indicates an error.
 *
 * @return
 *   0 on success, -1 on failure.
 */
int naaice_write_data(struct naaice_communication_context *comm_ctx,
                      uint8_t fncode);

/**
 * @brief Post a receive request for an MRSP message.
 *
 * Posts a receive request to the completion queue for an MRSP message.
 * The request specifies the memory region to be written to (the MRSP
 * region in this case).
 *
 * @param comm_ctx
 *   Pointer to the communication context describing the connection.
 *
 * @return
 *   0 on success, -1 on failure.
 */
int naaice_post_recv_mrsp(struct naaice_communication_context *comm_ctx);

/**
 * @brief Post a receive request for a memory region write.
 *
 * Posts a receive request for a memory region write. Only the final
 * memory region write (the one with an immediate value) requires a receive
 * request; regular RDMA writes without an immediate do not consume a receive
 * request.
 *
 * The memory region specified in the receive request is the MRSP region.
 * This is a placeholder, as the actual region written to is determined by
 * the sender.
 *
 * @param comm_ctx
 *   Pointer to the communication context describing the connection.
 *
 * @return
 *   0 on success, -1 on failure.
 */
int naaice_post_recv_data(struct naaice_communication_context *comm_ctx);

/**
 * @brief Initialize RDMA resources.
 *
 * Allocates and initializes the necessary RDMA resources, including a
 * protection domain, completion channel, completion queue, and queue pair.
 *
 * @param comm_ctx
 *   Pointer to the communication context describing the connection.
 *
 * @return
 *   0 on success, -1 on failure.
 */
int naaice_init_rdma_resources(struct naaice_communication_context *comm_ctx);

/**
 * @brief Execute the MRSP protocol in a blocking manner.
 *
 * Performs all steps of the Memory Region Setup Protocol (MRSP) in a
 * blocking fashion, ensuring that memory regions are properly advertised,
 * requested, and acknowledged before returning.
 *
 * @param comm_ctx
 *   Pointer to the communication context describing the connection.
 *
 * @return
 *   0 on success, -1 on failure.
 */
int naaice_do_mrsp(struct naaice_communication_context *comm_ctx);

/**
 * @brief Perform the complete data transfer in a blocking manner.
 *
 * Executes all steps of the data transfer, including writing data to the
 * NAA, waiting for the NAA to complete computation, and receiving the
 * resulting data back. This function operates in a blocking fashion.
 *
 * @param comm_ctx
 *   Pointer to the communication context describing the connection.
 *
 * @return
 *   0 on success, -1 on failure.
 */
int naaice_do_data_transfer(struct naaice_communication_context *comm_ctx);

/**
 * @brief Set the number of bytes to send from a memory region.
 *
 * Specifies how many bytes should be sent from the given memory region
 * during data transfer. Passing 0 resets the size to the original size
 * of the memory region.
 *
 * @param comm_ctx
 *   Pointer to the communication context describing the connection.
 *
 * @param mr_idx
 *   Index of the memory region to be modified.
 *
 * @param number_bytes
 *   Number of bytes to send from the specified memory region. 0 resets
 *   to the original size.
 *
 * @return
 *   0 on success, -1 on failure.
 */
int naaice_set_bytes_to_send(struct naaice_communication_context *comm_ctx,
                             int mr_idx, int number_bytes);

/** @} */ // end of doxygen group PublicFunctions

#ifdef __cplusplus
}
#endif

#endif
