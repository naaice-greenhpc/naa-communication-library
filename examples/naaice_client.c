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
 * naaice_client.c
 *
 * Application implementing a basic use case of the AP1 NAAICE communication
 * layer.
 *
 * For use in conjunction with naaice_server.c.
 *
 * Florian Mikolajczak, florian.mikolajczak@uni-potsdam.de
 * Dylan Everingham, everingham@zib.de
 *
 * 26-01-2024
 *
 *****************************************************************************/

/* Dependencies **************************************************************/

#include <naaice.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>
#include <ulog.h>
#include <unistd.h>

/* Constants *****************************************************************/

#define CONNECTION_PORT 12345
#define FNCODE 1

// Number of times to repeat the RPC.
#define N_INVOKES 2

/* Main **********************************************************************/

/**
 * Command line arguments:
 *  local-ip, ex. 10.3.10.135 (optional)
 *  remote-ip, ex. 10.3.10.136
 *  number-of-regions, ex. 1
 *  'region-sizes', ex '1024'
 */
int main(int argc, char *argv[]) {
  ulog_set_level(LOG_LEVEL);

  log_info("-- Handling Command Line Arguments --\n");

  // Check number of arguments.
  // TODO add command line parser getopt
  if ((argc != 4) && (argc != 5)) {
    log_error("Wrong number of arguments. use: "
              "./naaice_client [local-ip] remote-ip number-of-regions "
              "'region-sizes'\n"
              "Example: ./naaice_client 10.3.10.134 10.3.10.135 1 '1024'\n");
    return -1;
  };

  // Check if optional local IP argument was provided.
  int arg_offset = (argc == 4) ? 0 : 1;
  char *local_ip = (argc == 4) ? NULL : argv[1];
  char *remote_ip = argv[1 + arg_offset];

  // Check against maximum number of memory regions.
  char *ptr;
  long int params_amount = strtol(argv[2 + arg_offset], &ptr, 10);
  if (params_amount < 1 || params_amount > MAX_MRS) {
    log_error("Chosen number of arguments %ld is not supported.\n",
              params_amount);
    return -1;
  }

  log_debug("Connecting to %s from %s, using %d memory regions", remote_ip,
            local_ip, params_amount);

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
        log_error("Higher number of memory regions requested "
                  "than size information given.\n");
        return -1;
      }
      break;
    }
    param_sizes[i] = atoi(token);
  }

  // Set parameter values.
  // For this test, set each parameter to just be an array of chars, each with
  // the value of the number parameter it is.
  // i.e. the first parameter is an array of chars of value 0, the second is an
  // array of chars of value 1, etc.
  char *params[params_amount];
  for (unsigned char i = 0; i < params_amount; i++) {

    params[i] = (char *)malloc(param_sizes[i] * sizeof(char));
    if (params[i] == NULL) {
      log_error("Failed to allocate memory for parameters.\n");
      return -1;
    }

    params[i] = (char *)memset(params[i], 10 + i, param_sizes[i]);
  }

  // Communication context struct.
  // This will hold all information necessary for the connection.
  log_info("-- Initializing Communication Context --\n");
  struct naaice_communication_context *comm_ctx = NULL;

  // Initialize the communication context struct.
  if (naaice_init_communication_context(&comm_ctx, 0, param_sizes, params,
                                        params_amount, 0, 0, FNCODE, local_ip,
                                        remote_ip, CONNECTION_PORT)) {
    return -1;
  }

  // Now, handle connection setup.
  log_info("-- Setting Up Connection --\n");
  if (naaice_setup_connection(comm_ctx)) {
    return -1;
  }

  // Specify input and output parameters.
  // As an example, specify the first n-1 regions as input and the last one as
  // output

  log_info("-- Specifying Input and Output Memory Regions --\n");
  uint8_t no_input_mrs = 0;
  if (params_amount == 1) {
    no_input_mrs = 1;
  } else {
    no_input_mrs = params_amount;
  }
  if (params_amount <= 0) {
    log_error("Please specify at least one parameter!\n");
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

  // Specify parameters which should only be sent once (e.g. for config data).
  // As an example, specify the first parameter as a single send region.

  // When the number of invocations > 1 and no_input_mrs == 1,
  // no region will be sent on the second call, causing the client to wait
  // indefinitely for a server reply.
  if (no_input_mrs > 1) {
    if (naaice_set_singlesend_mr(comm_ctx, 0)) {
      return -1;
    }
  }

  // Set immediate value which can be used for testbed configuration.
  uint8_t imm_bytes[4] = {0, 0, 0, 0};

  // Then, register the memory regions with IBV.
  log_info("-- Registering Memory Regions with IBV --\n");

  if (naaice_register_mrs(comm_ctx)) {
    return -1;
  }

  // specify the number of bytes to be sent of a specific memory region
  // As an example the seconds memory region should only send to bytes
  if (naaice_set_bytes_to_send(comm_ctx, 0, 2)) {
    return -1;
  }

  if (naaice_set_immediate(comm_ctx, imm_bytes)) {
    return -1;
  }

  // Do the memory region setup protocol.
  log_info("-- Doing MRSP --\n");
  if (naaice_do_mrsp(comm_ctx)) {
    return -1;
  }

  // Do the data transfer, including commnication of parameters to NAA,
  // waiting for calculation to complete, and receiving return parameter
  // back from NAA.
  // Repeat RPC N_INVOKES times.
  log_info("-- Doing Data Transfer --\n");
  for (int i = 0; i < N_INVOKES; i++) {

    log_info("-- RPC Invocation #%d --\n", i + 1);
    if (naaice_do_data_transfer(comm_ctx)) {
      return -1;
    }
    // reset the number of bytes to be sent from the second memory region to the
    // full size of the memory region
    if (naaice_set_bytes_to_send(comm_ctx, 0, -1)) {
      return -1;
    }
  }

  // Disconnect and cleanup.
  log_info("-- Cleaning Up --\n");
  if (naaice_disconnect_and_cleanup(comm_ctx)) {
    return -1;
  }

  // At this point, we can check the data for correctness.
  // For the simple SWNAA example, we expect all values in the last parameter
  // to have been incremented, and the other parameters to be unchanged.
  log_info("-- Print Results --\n");

  unsigned char *data = (unsigned char *)(params[params_amount - 1]);
  printf("Output of the last MR: ");
  for (unsigned int j = 0; j < param_sizes[params_amount - 1]; j++) {
    printf("%d ", data[j]);
    if (j >= 10) {
      printf("\n");
      break;
    }
  }

  for (int i = 0; i < params_amount; i++) {
    free(params[i]);
  }

  return 0;
}
