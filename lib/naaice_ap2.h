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
 * naaice_ap2.h
 *
 * Interface for NAAICE AP2 "MPI-Like" middleware layer.
 * 
 * Florian Mikolajczak, florian.mikolajczak@uni-potsdam.de
 * Dylan Everingham, everingham@zib.de
 * 
 * 07-02-2024
 * 
 *****************************************************************************/

#ifndef NAAICE_AP2_C_H
#define NAAICE_AP2_C_H

/**
 * TODO:
 * 
 * - Add support for configuration files:
 *      - File for NAA routine info
 *      - File for FPGA hardware info
 *      - File for connection info (addresses / ports)
 * 
 *  - Hash out RMS integration with these.
 * 
 *  - Implement timeouts.
 * 
 *  - Implement status struct. Probably just populate with some relevant info
 *      in the communication context struct.
 * 
 */

/* Dependencies **************************************************************/

#include <stddef.h>
#include <stdbool.h>
#include <naaice.h>   // Included here to get enum naaice_communication_state.

/* Enums **********************************************************************/
enum naa_error {
  NAA_SUCCESS = 0x00,    // Successful RPC.
  SOCKET_UNAVAIL = 0x01, // Socket unavailable
  KERNEL_TIMEOUT = 0x02  // Kernel timed out (time out to be defined).
  // 0x03-0x0f // reserved for the future
  // 0x10-0x7f // application specific errors
};
/* Structs *******************************************************************/

/**
 * @brief Struct to hold information about a single (input or output) parameter
 *        to an NAA routine.
 */ 
typedef struct naa_param_t {
  void *addr; // address to data region
  size_t size; // size of data region
  bool single_send; // send data only once (e.g. for configuration data)
} naa_param_t;


/**
 * @brief Struct to hold information about a NAA session.
 * 
 */
typedef struct naa_handle {
  unsigned int function_code; // encodes the function to be executed on the NAA
  struct naaice_communication_context *comm_ctx; // communication context for low level API
} naa_handle;


/**
 * @brief Struct to hold status information about the NAA session.
 * 
 */
typedef struct naa_status {
  enum naaice_communication_state state;
  enum naa_error naa_error;
  uint32_t bytes_received;
} naa_status;


/* Public Functions **********************************************************/

/**
 * @brief Configure memory regions and establish connection to remote.
 * 
 * @param function_code encodes calculation on peer
 * @param input_params array of naa_param_t structs used as input regions
 * @param input_amount number of input regions
 * @param output_params array of naa_param_t structs used as output regions
 * @param output_amount number of output regions
 * @param handle communication handle created by naa_create
 * @return int 0 if sucessful, -1 if not.
 */
int naa_create(unsigned int function_code, 
  naa_param_t *input_params, unsigned int input_amount,
  naa_param_t *output_params, unsigned int output_amount,
  naa_handle *handle);

/**
 * @brief Sends data to the peer.
 * 
 * @param handle communication handle created by naa_create
 * @return int 0 if sucessful, -1 if not.
 */
int naa_invoke(naa_handle *handle);

/**
 * @brief Waits in non-blocking mode for a receive.
 * 
 * @param handle 
 * @param flag 
 * @param status 
 * @return int 
 */
int naa_test(naa_handle *handle, bool *flag, naa_status *status);

/**
 * @brief Waits in blocking mode for a receive.
 * 
 * @param handle communication handle created by naa_create
 * @param status 
 * @return int 
 */
int naa_wait(naa_handle *handle, naa_status *status);

/**
 * @brief Terminates connection and cleans up the corresponding data structures.
 * 
 * @param handle communication handle created by naa_create
 * @return int if sucessful, -1 if not.
 */
int naa_finalize(naa_handle *handle);

#endif
