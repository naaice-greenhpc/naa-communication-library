#ifndef NAAICE_H
#define NAAICE_H
#endif
#include <inttypes.h>
#include <stdio.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
//#include <infiniband/verbs.h>
#include <sys/socket.h>
#include <limits.h>
#include <netdb.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#ifndef MAX_TRANSFER_LENGTH
#define MAX_TRANSFER_LENGTH 1073741824ll // 1 GB
//#define MAX_TRANSFER_LENGTH 1024 // test for multiple mrs
#endif
#ifndef NUMBER_OF_REPITITIONS
#define NUMBER_OF_REPITITIONS 10
#endif
#define CONNECTION_PORT 12345
//COMP_ERROR is used to simulate computation errors to check error handling
#define COMP_ERROR 0
#define RX_DEPTH 1025
#define ntohll be64toh
#define htonll htobe64
#define TIMEOUT_IN_MS 500

#define TRANSFER_LENGTH 1024
#define INT_DATA_SUBSTITUTE 11
#define MR_META_DATA_SIZE 1024
// size of MR for MR exchange
// type + padding: 32 + Maximum number of regions:UINT_MAX+1 * (Addr:64 +  size:32 + key:32) + requested size:64 / 8
#define MR_SIZE_MR_EXHANGE (32 + (UINT8_MAX + 1) * 2 * 64 + 64) / 8
extern struct timespec programm_start_time, init_done_time, mr_exchange_done_time,
    data_exchange_done_time[NUMBER_OF_REPITITIONS + 1];

//static uint64_t transfer_length;

enum message_id
{
  MSG_MR_ERR = 0x00,
  // memory region announcement and request , send by host
  MSG_MR_AAR = 0x01,
  // memory region announcement without request, usually send by FPGA
  MSG_MR_A = 0x02,
  //not used yet
  MSG_MR_R = 0x03, 
};

// TODO: change it to be able to communica multiple regions
struct naaice_mr_advertisement
{
  uint64_t addr;
  uint32_t rkey;
  uint32_t size;
};

struct naaice_mr_dynamic_hdr
{
  uint8_t count;
  uint8_t padding[2];
};
struct naaice_mr_request
{
  uint64_t size;
};
struct naaice_mr_error
{
  uint8_t code;
  uint8_t padding[2];
};

struct naaice_mr_hdr
{
  uint8_t type;
};

struct naaice_mr_peer
{
  uint64_t addr;
  uint32_t rkey;
  uint32_t size;
};

struct naaice_mr_local
{
  struct ibv_mr *ibv;
  char *addr;
  uint32_t size;
};

struct naaice_communication_context
{
  // connection properties
  struct rdma_cm_id *id;
  struct ibv_context *ibv_ctx;
  struct ibv_pd *pd;
  struct ibv_comp_channel *comp_channel;
  struct ibv_cq *cq;
  struct ibv_qp *qp;
  // memory regions
  // list of peer memory regions terminated by NULL
  struct naaice_mr_peer *mr_peer_data;
  // list of local memory regions terminated by NULL, used for data exchange
  struct naaice_mr_local *mr_local_data;

  // struct naaice_mr_peer   *mr_peer_message;
  //  list of local memory regions terminated by NULL, used for memory region exchange
  struct naaice_mr_local *mr_local_message;

  // States for host and FPGA
  // FPGA goes from:
  //  ready: listening on connections
  //  connected: posted recv and waiting for mr
  //  wait_data: waiting for workload and immediate to start number crunching
  //  Running: number crunching
  //  FINISHED: sent data to host, goes back to wait_data
  //
  // Host goes from:
  //  ready: address resoved
  //  connected: posted recv for mr answers
  //  mr_exchange: announced own reagion and waitng for mr of fpga
  //  wait_data: waiting for results

  enum
  {
    READY,
    CONNECTED,
    MR_EXCHANGE,
    WAIT_DATA,
    RUNNING,
    FINISHED // :)
  } state;
  uint64_t transfer_length;
  uint8_t no_local_mrs;
  uint8_t no_peer_mrs;
  uint32_t repititions_completed;
  uint64_t requested_size;
  uint64_t peer_advertised_length;
  uint8_t events_to_ack;
  uint8_t events_acked;
  uint8_t rdma_writes_done;
  uint8_t no_advertised_mrs;
  bool rdma_write_finished;
};
 /**
 * Called by Host (after addr resolution) and FPGA (on connection request). Communcation context is allocated as well as RMDA structures such as protection domain, completion channel, completion queue, queue pair. Returns 0 on success or -1 in case of error.
 * params:
 * rdma communcation id: communcation identifier, analogous to sockets
 */
int naaice_prepare_connection(struct naaice_communication_context *comm_ctx);

/**
 * RDMA event loop. RDMA communication is event-based. This event loop is used
 * for handling connection building and cleanup. Returns 0 on sucess, -1
 * otherwise params: rdma event returned by rdma_get_cm_event()
 */
int naaice_on_event_server(struct rdma_cm_event *ev,
                    struct naaice_communication_context *comm_ctx);

/**
 * Routine called after address resolution of the host/connection initiator.
 * During the routine route resolution for the resolved address is tried.
 * Returns 0 on sucess, -1 otherwise params: rdma communcation id: communcation
 * identifier, analogous to sockets
 */
int naaice_on_event_client(struct rdma_cm_event *ev,
                    struct naaice_communication_context *comm_ctx);

/**
 * Routine called after address resolution of the host/connection initiator.
 * During the routine route resolution for the resolved address is tried.
 * Returns 0 on sucess, -1 otherwise params: rdma communcation id: communcation
 * identifier, analogous to sockets
 */
int naaice_on_addr_resolved(struct rdma_cm_id *id, struct naaice_communication_context *comm_ctx);

/**
 * Routine called after route resolution of the host/connection initiator. During the routine communication parameters such as retries for non-acked messages are set and rdma_connect() is called to initiate the connection handshake. Returns 0 on sucess, -1 otherwise
 * params:
 * rdma communcation id: communcation identifier, analogous to sockets
 */
int naaice_on_route_resolved(struct rdma_cm_id *id);

/**
 * Routine called after event for connection request is triggered. Used on the FPGA/side passive partner in connection establishment. Sets communication parameters and rdma_accept() is called to accept connection request. Returns 0 on success, -1 otherwise
 * params:
 * rdma communcation id: communcation identifier, analogous to sockets
 */
int naaice_on_connection_requests(struct rdma_cm_id *id, struct naaice_communication_context *comm_ctx);
/**
 * Routine called by client after connection establishment. Only called by initiator.
 * Memory regions for message exchange and local data memory regions are
 * allocated and registered. Returns 0 on success, -1 otherwise params: rdma
 * communcation id: communcation identifier, analogous to sockets
 */
int naaice_on_connection_client(struct naaice_communication_context *comm_ctx);
/**
 * Routine called by server after connection establishment. Only called by initiator.
 * Memory regions for message exchange and local data memory regions are
 * allocated and registered. Returns 0 on success, -1 otherwise params: rdma
 * communcation id: communcation identifier, analogous to sockets
 */
int naaice_on_connection_server(struct naaice_communication_context *comm_ctx);

/**
 * Routine called after event disconnetion. Cleanup. MRs are deregistered and
 * all allocated memory is freed. Returns 0 on success, -1 otherwise params:
 *  naaice_communication_context *comm_ctx: structure for the communication
 * context. Holds all info of the connection and mrs
 */
int naaice_on_connection_client(struct naaice_communication_context *comm_ctx);

/**
 * Routine called after event disconnetion. Cleanup. MRs are deregistered and
 * all allocated memory is freed. Returns 0 on success, -1 otherwise params:
 *  naaice_communication_context *comm_ctx: structure for the communication
 * context. Holds all info of the connection and mrs
 */
int naaice_on_disconnect(struct naaice_communication_context *comm_ctx);

/**
 * Routine called any rdma operation is initiated. Finished rdma operation such as send/recv/read/write add element to the completion queue. Call leads to constant polling of the queue until an element is found. Events from the queue are acked and naaice_on_completion() is called to handle the completed operation. Returns 0 on success, -1 otherwise.
 * params:
 * naaice_communication_context *comm_ctx: structure for the communication context. Holds all info of the connection and mrs
 */ 
void naaice_poll_cq(struct naaice_communication_context *comm_ctx);

/**
 * Main Routine for data transfer. Called after each completed RDMA operation.
 * Usually the next RDMA operation is initiated or incoming data is handled.
 * Progression is ensured through a state-machine-like structure in the
 * commmunication context. params: naaice_communication_context *comm_ctx:
 * structure for the communication context. Holds all info of the connection and
 * mrs
 */
int naaice_on_completion_client(struct ibv_wc *wc,
                         struct naaice_communication_context *comm_ctx);
/**
 * Main Routine for data transfer. Called after each completed RDMA operation.
 * Usually the next RDMA operation is initiated or incoming data is handled.
 * Progression is ensured through a state-machine-like structure in the
 * commmunication context. params: naaice_communication_context *comm_ctx:
 * structure for the communication context. Holds all info of the connection and
 * mrs
 */
int naaice_on_completion_server(struct ibv_wc *wc,
                         struct naaice_communication_context *comm_ctx);

/**
 * Routine for sending out metadata for memory regions. 
 * params:
 * naaice_communication_context *comm_ctx: structure for the communication context. Holds all info of the connection and mrs
 * message_type: The type of message for the MR transfer. See protocol details in meetings from Nov22/Jan23 for details. 
 * requested-size: Size of memory we request in the communication partner in bytes. 0 if message type does not include a request
 */ 
void naaice_mr_exchange(struct naaice_communication_context *comm_ctx, enum message_id message_type,size_t requested_size);

/**
 * Routine for writing data into peer data MRs. Either a simple RDMA_WRITE or RDMA_WRITE_WITH_IMM (last write operation)
 * params:
 * naaice_communication_context *comm_ctx: structure for the communication context. Holds all info of the connection and mrs
 */ 
int naaice_write(struct naaice_communication_context *comm_ctx, uint errorcode);

/**
 * Routine for posting receives. Receives have to be posted for corresponding SEND operations and WRITE_WITH_IMM. 
 * params:
 * naaice_communication_context *comm_ctx: structure for the communication context. Holds all info of the connection and mrs
 */ 
int naaice_post_recv(struct naaice_communication_context *comm_ctx);

/** 
 * Routine for handling MR announcements. Peer information is saved-
 * naaice_communication_context *comm_ctx: structure for the communication context. Holds all info of the connection and mrs.
*/
int naaice_handle_mr_announce(struct naaice_communication_context *comm_ctx);
/** 
 * Routine for handling MR reqs. So far the requested size is saved and  MRs of the same size as the ones announced are generated. 
 * This will be changed later, when we work with non-homogenous MRs between both communication partners. 
 * naaice_communication_context *comm_ctx: structure for the communication context. Holds all info of the connection and mrs.
*/
int naaice_handle_mr_req(struct naaice_communication_context *comm_ctx);
/** 
 * Routine for handling MR announcemnets and request. Combintion of the routines for handling announcements and requests
 * naaice_communication_context *comm_ctx: structure for the communication context. Holds all info of the connection and mrs.
*/
int naaice_handle_mr_announce_and_req(struct naaice_communication_context *comm_ctx);
/** 
 * Routine for creating local data MRs. Called by the requesting side/client. MRs are created based on the requested transfer length and the maximum transfer lenght for a single RDMA operation. 
*/
int naaice_prepare_local_mrs(struct naaice_communication_context *comm_ctx);

//void * poll_cq(void *);

/*void naaice_mr_response(struct rdma_cm_id *id);

void naaice_send(struct rdma_cm_id *id);*/
double timediff(struct timespec start, struct timespec end);

int memvcmp(void *memory, unsigned char val, unsigned int size);