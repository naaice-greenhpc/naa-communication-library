// Interface for the NAAICE AP2 "MPI-like" middleware layer.

#ifndef NAAICE_AP2_C_H
#define NAAICE_AP2_C_H

/* Dependencies **************************************************************/

#include <stdbool.h>
#include <stddef.h>

#include "naaice.h"  // Included here to get enum naaice_communication_state.

/* Structs/Typedefs **********************************************************/

// Numerical code identifying the function/accelerator requested by the application.
typedef uint32_t naa_function_code_t;

// A single input or output parameter for an NAA routine: the data region's
// address, size, and whether it is sent only once.
typedef struct naa_param_t {
  void *addr;
  size_t size;       // in bytes
  bool single_send;  // if true, sent only on the first communication with the
                     // routine (typically configuration data)
} naa_param_t;

// Handle to an active NAA session: the function code to execute and the
// associated low-level communication context.
typedef struct naa_handle {
  naa_function_code_t function_code;

  // Communication context used for low-level API operations.
  struct naaice_communication_context *comm_ctx;
} naa_handle;

// Status information for an NAA session.
typedef struct naa_status {
  naaice_communication_state state;

  uint32_t user_immediate;  // response user immediate (24 bit)

  uint64_t bytes_received;
} naa_status;

#ifdef __cplusplus
extern "C" {
#endif

/* Public Functions **********************************************************/

/*
 * Finds an NAA matching the requested function code, prepares the connection,
 * and exchanges memory region information between HPC node and NAA. Initializes
 * the handle for this session. Returns 0 on success, -1 on failure.
 *
 * The NAA's IP address and socket ID come from the resource management system
 * (Slurm) at job deployment. Connection establishment follows the InfiniBand
 * standard. This call registers the parameter addresses as ibverbs memory
 * regions, hiding the memory region semantics from the user. All regions, both
 * input and output, are announced to the NAA here, so a region cannot switch
 * between input and output across iterations.
 *
 * input_params/output_params are arrays of naa_param_t describing the input and
 * output memory regions.
 */
int naa_create(const naa_function_code_t function_code, naa_param_t *input_params,
               unsigned int input_amount, naa_param_t *output_params, unsigned int output_amount,
               naa_handle *handle);

/*
 * Sends input data to the peer and triggers the corresponding NAA routine.
 * Posts the RDMA writes for the current session. Returns 0 on success, -1 on
 * failure.
 *
 * The transfer uses RDMA_WITH_IMM: for a transfer of n operations, n-1 are
 * plain RDMA_WRITE and the last is RDMA_WITH_IMM carrying the function code as
 * immediate data. RDMA_WITH_IMM signals the end of transfer and starts the
 * computation on the NAA.
 */
int naa_invoke(naa_handle *handle);

/*
 * Non-blocking check for a completed receive, much like MPI_Test. Polls the
 * completion queue of the data transfer's queue pair. Sets flag to true and
 * fills status if the operation is complete; sets flag to false otherwise, in
 * which case status is undefined. Returns 0 on success, -1 on failure.
 */
int naa_test(naa_handle *handle, bool *flag, naa_status *status);

/*
 * Blocking wait for a completed receive, much like MPI_Wait. Polls the
 * completion queue of the data transfer's queue pair and returns once data has
 * been written back to the HPC node, filling status with the completed
 * operation's information. Returns 0 on success, -1 on failure.
 */
int naa_wait(naa_handle *handle, naa_status *status);

// Terminates the connection and cleans up the session's data structures.
// Returns 0 on success, -1 on failure.
int naa_finalize(naa_handle *handle);

#ifdef __cplusplus
}
#endif

#endif
