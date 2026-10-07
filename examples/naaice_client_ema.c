/*
 * Example client for the NAAICE AP1 communication layer, using PERFACCT's EMA
 * for CPU energy measurement. Use together with naaice_server.c.
 */

/* Dependencies **************************************************************/

#include <EMA.h>
#include <naaice.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>
#include <ulog.h>
#include <unistd.h>

/* Constants *****************************************************************/
#define FNCODE 1

// Number of times to repeat the RPC.
#define N_INVOKES 3
// Number of args with local IP
#define NUM_ARG_LOCAL_IP 5
// Number of args without local IP
#define NUM_ARG_NO_LOCAL_IP 4

/* Main **********************************************************************/

/* Command line arguments:
 *   local-ip, e.g. 10.3.10.135 (optional)
 *   remote-ip, e.g. 10.3.10.136
 *   number-of-regions, e.g. 1
 *   'region-sizes', e.g. '1024'
 */
int main(int argc, char *argv[]) {
#ifndef ULOG_BUILD_DISABLED
  ulog_output_level_set_all(LOG_LEVEL);
#endif

  ulog_info("-- Handling Command Line Arguments --\n");

  // Check number of arguments.
  if ((argc != NUM_ARG_LOCAL_IP) && (argc != NUM_ARG_NO_LOCAL_IP)) {
    ulog_error(
        "Wrong number of arguments. use: "
        "./naaice_client\n"
        "\t[local-ip]\n"
        "\tremote-ip\n"
        "\tnumber-of-regions\n"
        "\t'region-sizes'\n"
        "Example: ./naaice_client 10.3.10.134 10.3.10.135 1 '1024'\n");
    return -1;
  };

  // Check if optional local IP argument was provided.
  int arg_offset = (argc == NUM_ARG_NO_LOCAL_IP) ? 0 : 1;
  char *local_address = (argc == NUM_ARG_NO_LOCAL_IP) ? NULL : argv[1];
  char *remote_address = argv[1 + arg_offset];

  // Check against maximum number of memory regions.
  char *ptr;
  long int params_amount = strtol(argv[2 + arg_offset], &ptr, 10);
  if (params_amount < 1 || params_amount > MAX_MRS) {
    ulog_error("Chosen number of arguments %ld is not supported.\n", params_amount);
    return -1;
  }

  // Get sizes of memory regions from command line.
  size_t param_sizes[params_amount];

  // First (non-metadata) region.
  char *token = strtok(argv[3 + arg_offset], " ");
  param_sizes[0] = atoi(token);

  // If more sizes provided than the specified number of regions, exit.
  int i = 0;
  while (i <= params_amount) {
    i++;
    token = strtok(NULL, " ");
    if (token == NULL) {
      if (i < params_amount) {
        ulog_error(
            "Higher number of memory regions requested "
            "than size information given.\n");
        return -1;
      }
      break;
    }
    param_sizes[i] = atoi(token);
  }

  // Fill each parameter with a char array whose value equals its index:
  // first parameter all 0s, second all 1s, and so on.
  char *params[params_amount];
  for (unsigned char i = 0; i < params_amount; i++) {
    params[i] = (char *)malloc(param_sizes[i] * sizeof(char));
    if (params[i] == NULL) {
      ulog_error("Failed to allocate memory for parameters.\n");
      return -1;
    }

    params[i] = (char *)memset(params[i], i, param_sizes[i]);
  }

  // Measure client energy usage via EMA. Initialize it first.
  int err_ema = EMA_init(NULL);
  if (err_ema) {
    return -1;
  }

  // Declare and define an EMA measurement region.
  EMA_REGION_DECLARE(ema_region);
  EMA_REGION_DEFINE(&ema_region, "ema_region");

  // Start the measurement.
  EMA_REGION_BEGIN(ema_region);

  // Communication context, holding all state for the connection.
  ulog_info("-- Initializing Communication Context --\n");
  struct naaice_communication_context *comm_ctx = NULL;

  // Initialize the communication context.
  if (naaice_init_communication_context(&comm_ctx, 0, param_sizes, params, params_amount, 0, 0,
                                        FNCODE, local_ip, argv[1 + arg_offset],
                                        SERVER_CONNECTION_PORT)) {
    return -1;
  }

  // Set up the connection.
  ulog_info("-- Setting Up Connection --\n");
  if (naaice_setup_connection(comm_ctx)) {
    return -1;
  }

  // Mark the first two parameters as inputs and the second as output.
  ulog_info("-- Specifying Input and Output Memory Regions --\n");
  if (naaice_set_input_mr(comm_ctx, 0)) {
    return -1;
  }
  if (naaice_set_input_mr(comm_ctx, 1)) {
    return -1;
  }
  if (naaice_set_output_mr(comm_ctx, 1)) {
    return -1;
  }

  // Register the memory regions with IBV.
  ulog_info("-- Registering Memory Regions with IBV --\n");
  if (naaice_register_mrs(comm_ctx)) {
    return -1;
  }

  // Configure NAA-internal memory regions; here one at address 0, size 32.
  ulog_info("-- Specifying NAA Internal Memory Regions --\n");
  uintptr_t internal_addrs[1] = {0};
  size_t internal_sizes[1] = {32};
  if (naaice_set_internal_mrs(comm_ctx, 1, internal_addrs, internal_sizes)) {
    return -1;
  }

  /*
  // Set metadata (i.e. return address).
  // For our example, the return parameter is the last one.
  unsigned char return_param_idx = params_amount - 1;
  ulog_info("-- Setting Metadata --\n");
  if (naaice_set_metadata(comm_ctx, (uintptr_t) params[return_param_idx])) {
    return -1; }
  */

  // Do the memory region setup protocol.
  ulog_info("-- Doing MRSP --\n");
  if (naaice_do_mrsp(comm_ctx)) {
    return -1;
  }

  // Send parameters to the NAA, wait for the computation, and receive the
  // return parameter back. Repeated N_INVOKES times.
  ulog_info("-- Doing Data Transfer --\n");
  for (int i = 0; i < N_INVOKES; i++) {
    ulog_info("-- invocation #%d --\n", i);
    if (naaice_do_data_transfer(comm_ctx)) {
      return -1;
    }
  }

  // Disconnect and cleanup.
  ulog_info("-- Cleaning Up --\n");
  if (naaice_disconnect_and_cleanup(comm_ctx)) {
    return -1;
  }

  // Stop the measurement.
  EMA_REGION_END(ema_region);

  // Finalize EMA.
  EMA_finalize();

  // For the simple SWNAA example, the last parameter should be incremented
  // and the others unchanged.
  ulog_info("-- Checking Results --\n");
  for (unsigned char i = 0; i < params_amount; i++) {
    bool success = true;
    unsigned char *data = (unsigned char *)(params[i]);
    for (unsigned int j = 0; j < param_sizes[i]; j++) {
      unsigned char el = data[j];

      if (i == params_amount - 1) {
        if (el != (i + N_INVOKES)) {
          success = false;
        }
      } else {
        if (el != i) {
          success = false;
        }
      }
    }

    ulog_info("Parameter %u: first element: %u. Success? %s\n", i, data[0], success ? "yes" : "no");
  }

  return 0;
}