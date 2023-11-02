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
 * 12-10-2023
 * 
 *****************************************************************************/

#ifndef NAAICE_SWNAA_H
#define NAAICE_SWNAA_H

/* Dependencies **************************************************************/

#include <naaice.h>

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
 * 
 * returns:
 *  0 if sucessful, -1 if not.
 */

/**
 * naaice_swnaa_init_communication_context:
 *  Initializes communication context struct.
 *  After a call to this function, the provied communication context struct is
 *  ready to be passed to all other SWNAA AP1 functions.
 *  Compared to the base NAAICE version, this SWNAA does not take parameters
 *  for the local and remote network addresses or those which describe the
 *  NAA routine's parameters.
 * 
 * params:
 *  naaice_communication_context **comm_ctx: (return param)
 *    Double pointer to communication context struct to be initialized.
 *    Should not point to an existing struct; the struct is allocated
 *    and returned by this function.
 *  uint16_t port:
 *    Value specifying connection port, ex. 12345.
 * 
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
 *  These represent memory region writes from the host. 
 * 
 * params:
 *  naaice_communication_context *comm_ctx:
 *    Pointer to struct describing the connection.
 * 
 * returns:
 *  0 if sucessful, -1 if not.
 */
int naaice_swnaa_handle_work_completion_mrsp(struct ibv_wc *wc,
  struct naaice_communication_context *comm_ctx);

int naaice_swnaa_poll_cq_nonblocking_mrsp(
  struct naaice_communication_context *comm_ctx);

int naaice_swnaa_poll_cq_blocking_mrsp(
  struct naaice_communication_context *comm_ctx);

int naaice_swnaa_handle_mr_announce(
  struct naaice_communication_context *comm_ctx);

int naaice_swnaa_handle_mr_announce_and_request(
  struct naaice_communication_context *comm_ctx);

int naaice_swnaa_send_message(struct naaice_communication_context *comm_ctx,
  enum message_id message_type, uint8_t errorcode);

int naaice_swnaa_post_recv_data(struct naaice_communication_context *comm_ctx);

int naaice_swnaa_handle_work_completion_data(struct ibv_wc *wc,
  struct naaice_communication_context *comm_ctx);

int naaice_swnaa_poll_cq_nonblocking_data(
  struct naaice_communication_context *comm_ctx);

int naaice_swnaa_poll_cq_blocking_data(
  struct naaice_communication_context *comm_ctx);

int naaice_swnaa_handle_metadata(
  struct naaice_communication_context *comm_ctx);

int naaice_swnaa_write_data(
  struct naaice_communication_context *comm_ctx, uint8_t errorcode);

int naaice_swnaa_disconnect_and_cleanup(
  struct naaice_communication_context *comm_ctx);

#endif