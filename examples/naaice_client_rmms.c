/*
 * Example client for the NAAICE AP1 communication layer with the remote memory
 * management service. Use with naaice_server.c and remote_malloc_server.py.
 */

/* Dependencies **************************************************************/

#include <naaice.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>
#include <ulog.h>
#include <unistd.h>

#include "malloc_client.h"

/* Constants *****************************************************************/
#define FNCODE 3

// Number of times to repeat the RPC.
#define N_INVOKES 10

// Number of args with local IP
#define NUM_ARG_LOCAL_IP 8
// Number of args without local IP
#define NUM_ARG_NO_LOCAL_IP 7

/* Main **********************************************************************/
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
        "\tremote_malloc_address\n"
        "\tremote_malloc_port\n"
        "\tFPGA mnemonic\n\n"

        "Example: ./naaice_client_rmms 10.3.10.134 10.3.10.135 1 '1024' 10.3.10.135 54321 "
        "EL_ZERO\n"
        "");
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

  ulog_debug("Connecting to %s from %s, using %d memory regions", remote_address, local_address,
             params_amount);

  // Get sizes of memory regions from command line.
  size_t param_sizes[params_amount];

  // First region.
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

  // Extra values needed to talk to the rmms server.
  char *rmms_address = argv[4 + arg_offset];
  int rmms_port = atoi(argv[5 + arg_offset]);
  char *fpga_mnemomic = argv[6 + arg_offset];

  // Fill each parameter with a char array whose value equals its index:
  // first parameter all 0s, second all 1s, and so on.
  char *params[params_amount];
  for (unsigned char i = 0; i < params_amount; i++) {
    params[i] = (char *)malloc(param_sizes[i] * sizeof(char));
    if (params[i] == NULL) {
      ulog_error("Failed to allocate memory for parameters.\n");
      return -1;
    }

    params[i] = (char *)memset(params[i], 10 + i, param_sizes[i]);
  }

  // Communication context, holding all state for the connection.
  ulog_info("-- Initializing Communication Context --\n");

  struct naaice_communication_context *comm_ctx = NULL;
  struct naaice_rmms_data *rmms_data = NULL;

  ulog_info(
      "local ip: %s\n"
      "remote ip: %s\n"
      "rmms ip: %s\n"
      "rmms port: %d\n"
      "fpga mnenomic: %s\n"
      "param size 0: %d\n",
      local_address, remote_address, rmms_address, rmms_port, fpga_mnemomic, param_sizes[0]);

  // Initialize rmms communication data.
  if (naaice_init_rmms_data(&rmms_data, rmms_address, rmms_port, fpga_mnemomic, NULL)) {
    return -1;
  }

  // Initialize the communication context.
  if (naaice_init_communication_context_with_rmms(&comm_ctx, param_sizes, params, params_amount, 0,
                                                  0, FNCODE, local_address, remote_address,
                                                  SERVER_CONNECTION_PORT, rmms_data)) {
    return -1;
  }

  // Set up the connection.
  ulog_info("-- Setting Up Connection --\n");
  if (naaice_setup_connection(comm_ctx)) {
    return -1;
  }

  // Mark the first n-1 regions as input and the last one as output.
  ulog_info("-- Specifying Input and Output Memory Regions --\n");
  uint8_t no_input_mrs = params_amount;
  if (params_amount <= 0) {
    ulog_error("Please specify at least one parameter!\n");
    return -1;
  }

  for (uint8_t i = 0; i < no_input_mrs; i++) {
    if (naaice_set_input_mr(comm_ctx, i)) {
      return -1;
    }
  }

  if (naaice_set_output_mr(comm_ctx, params_amount - 1)) {
    return -1;
  }

  // Mark single-send regions (e.g. config data); here the first one.
  // With only one input region, later invocations would send nothing and hang.
  if (no_input_mrs > 1) {
    if (naaice_set_singlesend_mr(comm_ctx, 0)) {
      return -1;
    }
  }

  // Set immediate value which can be used for testbed configuration.
  uint8_t imm_bytes[4] = {0, 0, 0, 0};

  // Register the memory regions with IBV.
  ulog_info("-- Registering Memory Regions with IBV --\n");

  if (naaice_register_mrs(comm_ctx)) {
    return -1;
  }

  // Limit how many bytes a given region sends; here region 0 sends only 2.
  if (naaice_set_bytes_to_send(comm_ctx, 0, 2)) {
    return -1;
  }

  if (naaice_set_immediate(comm_ctx, imm_bytes)) {
    return -1;
  }

  // Do the memory region setup protocol.
  ulog_info("-- Doing MRSP --\n");
  if (naaice_do_mrsp(comm_ctx)) {
    return -1;
  }

  // Send parameters to the NAA, wait for the computation, and receive the
  // return parameter back. Repeated N_INVOKES times.
  ulog_info("-- Doing Data Transfer --\n");
  for (int i = 0; i < N_INVOKES; i++) {
    ulog_info("-- RPC Invocation #%d --\n", i + 1);
    if (naaice_do_data_transfer(comm_ctx)) {
      return -1;
    }
    // Reset region 0 to send its full size again.
    if (naaice_set_bytes_to_send(comm_ctx, 0, -1)) {
      return -1;
    }
  }

  // Disconnect and cleanup.
  ulog_info("-- Cleaning Up --\n");
  if (naaice_disconnect_and_cleanup(comm_ctx)) {
    return -1;
  }

  // For the simple SWNAA example, the last parameter should be incremented
  // and the others unchanged.
  ulog_info("-- Print Results --\n");

  unsigned char *data = (unsigned char *)(params[params_amount - 1]);
  ulog_info("Output of the last MR: ");
  for (unsigned int j = 0; j < param_sizes[params_amount - 1]; j++) {
    ulog_info("%d ", data[j]);
    if (j >= 10) {
      ulog_info("\n");
      break;
    }
  }

  for (int i = 0; i < params_amount; i++) {
    free(params[i]);
  }

  return 0;
}
