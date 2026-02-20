/****************************************************************************
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
 * Hannes Signer, signer@uni-potsdam.de
 *
 * 18-12-2025
 *
 *****************************************************************************/

#ifndef NAAICE_AP2_C_H
#define NAAICE_AP2_C_H

/* Dependencies **************************************************************/

#include "naaice.h" // Included here to get enum naaice_communication_state.
#include <stdbool.h>
#include <stddef.h>

/** @defgroup StructsEnumsMiddleware Structs & Enums */
/** @defgroup PublicFunctionsMiddleware Functions */

/* Enums **********************************************************************/
/**
 * @ingroup StructsEnumsMiddleware
 * @brief Error codes returned by NAA routines.
 *
 * Defines possible error values for RPCs or communication failures
 * between host and NAA.
 */
enum naa_error {
  /// Successful RPC.
  NAA_SUCCESS = 0x00,

  /// Socket unavailable.
  SOCKET_UNAVAIL = 0x01,

  /// Kernel timed out (timeout definition TBD).
  KERNEL_TIMEOUT = 0x02,

  // 0x03-0x0f: Reserved for future use.
  // 0x10-0x7f: Application-specific errors.
};

/* Structs/Typedefs **********************************************************/

/**
 * @brief Numerical code for the function/accelerator to be used by the
 * application
 *
 */
typedef uint32_t naa_function_code_t;

/**
 * @ingroup StructsEnumsMiddleware
 * @struct naa_param_t
 * @brief Represents a single parameter (input or output) for an NAA routine.
 *
 * Holds information about the data region corresponding to a parameter,
 * including its address, size, and whether it should be sent only once
 * during the connection (e.g., for configuration data).
 *
 */
typedef struct naa_param_t {
  void *addr;       ///< Pointer to the data region
  size_t size;      ///< Size of the data region, in bytes
  bool single_send; ///< Indicates that the parameter should be sent only once.
                    ///< If true, the parameter is sent only during the first
                    ///< communication with the NAA routine (typically for
                    ///< configuration data)
} naa_param_t;

/**
 * @ingroup StructsEnumsMiddleware
 * @brief Represents a handle to a NAA session.
 *
 * Holds information about an active NAA session, including the function
 * code specifying the routine to execute and the associated low-level
 * communication context.
 */
typedef struct naa_handle {
  /// Function code specifying the routine to be executed on the NAA.
  naa_function_code_t function_code;

  /// Pointer to the communication context used for low-level API operations.
  struct naaice_communication_context *comm_ctx;
} naa_handle;

/**
 * @ingroup StructsEnumsMiddleware
 * @brief Status information for a NAA session.
 *
 * Holds the current state of the communication, any error codes returned
 * by the NAA, and the number of bytes received so far.
 */
typedef struct naa_status {
  /// Current state of the communication session.
  naaice_communication_state state;

  /// Last error code returned by the NAA.
  enum naa_error naa_error;

  /// Number of bytes received during this session.
  uint64_t bytes_received;
} naa_status;

#ifdef __cplusplus
extern "C" {
#endif

/* Public Functions **********************************************************/

/**
 * @ingroup PublicFunctionsMiddleware
 * @brief Finding IP address and socket ID for an NAA matching required function
 code. Prepare connection, register and exchange memory region information
 between HPC node and NAA.

 IP address and socket ID are already known to HPC node. Info is retrieved from
 resource management system (Slurm) at creation/deployment of slurm job. User
 knows function code for method/calculation to outsource to NAA. Connection to
 NAA is done by connection establishment protocol from the Infiniband standard.
 During connection preparation, the HPC nodes allocates buffers for memory re-
 gions and resolves route to NAA. After connection establishment, memory region
 information is exchanged between HPC node and NAA. The protocol for this was
 designed in NAAICE AP1.

 This method will register the addresses of the parameters with ibverbs as mem-
 ory regions, hiding the memory region semantic from the user. All memory re-
 gions, for both input and output parameters are announced to the NAA during
 naa_create(). Therefore, memory regions can not be changed from input to
 output between iterations. Currently, no example has been found where this is
 necessary. The handle object was previously returned by the library and
 includes information on how to connect to the right NAA. The resource
 management system will provide information on the IP of the NAA and socket ID
 of the NAA.

 * @param function_code Function code specifying the routine to execute on the
 * NAA.
 * @param input_params Array of ::naa_param_t structs representing input
 * regions.
 * @param input_amount Number of input memory regions.
 * @param output_params Array of ::naa_param_t structs representing output
 * regions.
 * @param output_amount Number of output memory regions.
 * @param handle Pointer to a ::naa_handle struct to be initialized for this
 * session.
 * @return int 0 if successful, -1 if an error occurred.
 */
int naa_create(const naa_function_code_t function_code,
               naa_param_t *input_params, unsigned int input_amount,
               naa_param_t *output_params, unsigned int output_amount,
               naa_handle *handle);

/**
 * @ingroup PublicFunctionsMiddleware
 * @brief Sends input data to the peer and triggers the corresponding NAA
 * routine.
 *
 * Initiates the data transfer for the current session using the provided
 * communication handle. Handles posting RDMA writes and waiting for the
 * remote computation to complete.
 *
 *
 * \note Data transfer is done with RDMA_WITH_IMM. If the transfer requires n >
 * 1 operations, n − 1 RDMA_WRITE operations are done. The last writing
 * operation is RDMA_WITH_IMM, where the immediate data value is the function
 * code. RDMA_WITH_IMM signals the end of the data transfer to the NAA and
 * initiates calculations on the NAA (RPC start)
 *
 * @param handle Pointer to a ::naa_handle created by ::naa_create.
 * @return int 0 if successful, -1 if an error occurred.
 */
int naa_invoke(naa_handle *handle);

/**
 * @ingroup PublicFunctionsMiddleware
 * @brief Waits in non-blocking mode for a receive.
 *
 * Much like MPI_TEST, the naa_test call is non-blocking and polls the
 * completion queue of the queue pair associated with the data transfer.
 *
 * A call to naa_test returns flag=true if the operation identified by handle is
 * complete. * In such a case, the status object is set to contain information
 * on the completed operation. The call returns flag = false if the operation is
 * not complete. In this case, the value of the status object is undefined.
 *
 *
 *
 * @param handle
 * @param flag
 * @param status
 * @return int
 */
int naa_test(naa_handle *handle, bool *flag, naa_status *status);

/**
 * @ingroup PublicFunctionsMiddleware
 * @brief Waits in blocking mode for a receive.
 *
 * Much like MPI_WAIT, the naa_wait call is blocking and polls the completion
 queue
 * of the queue pair associated with the data transfer. naa_wait returns, when
 data
 * has been written back to the HPC node.
 * The call returns with the information on the completed operation stored in
 the
 * status variable.

 * @param handle communication handle created by naa_create
 * @param status
 * @return int
 */
int naa_wait(naa_handle *handle, naa_status *status);

/**
 * @ingroup PublicFunctionsMiddleware
 * @brief Terminates connection and cleans up the corresponding data structures.
 *
 * @param handle communication handle created by naa_create
 * @return int if sucessful, -1 if not.
 */
int naa_finalize(naa_handle *handle);

#ifdef __cplusplus
}
#endif

#endif
