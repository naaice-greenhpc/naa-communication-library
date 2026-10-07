/*
 * Example client for the NAAICE AP2 middleware layer.
 * Use together with naaice_server.c.
 */

/* Dependencies **************************************************************/

#include <ulog.h>

#include "naaice_ap2.h"

/* Constants *****************************************************************/

// Function code specifying RPC.
#define FNCODE 1

// Number of times to repeat the RPC.
#define N_INVOKES 100

/* Main **********************************************************************/
/* Command line arguments:
 *   number-of-regions, e.g. 1
 *   'region-sizes', e.g. '64, 128'
 */
int main(int argc, char *argv[]) {
#ifndef ULOG_BUILD_DISABLED
  ulog_output_level_set_all(LOG_LEVEL);
#endif

  ulog_info("-- Handling Command Line Arguments --\n");

  // Check number of arguments.
  if (argc != 3) {
    ulog_error(
        "Wrong number of arguments. use: "
        "./naaice_client_ap2 number-of-regions 'region-sizes'\n"
        "Example: ./naaice_client_ap2 2 '64, 128'\n");
    return -1;
  };

  // Check against maximum number of memory regions.
  char *ptr;
  long int params_amount = strtol(argv[1], &ptr, 10);
  if (params_amount < 1 || params_amount > MAX_MRS) {
    ulog_error("Chosen number of arguments %ld is not supported.\n", params_amount);
    return -1;
  }

  // Get sizes of memory regions from command line.
  size_t param_sizes[params_amount];

  // First region.
  char *token = strtok(argv[2], " ");
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

  // Handle holding all state for a NAA session.
  struct naa_handle *handle = (naa_handle *)calloc(1, sizeof(struct naa_handle));
  if (!handle) {
    ulog_error("Failed to create naa handle. Exiting.\n");
    return -1;
  }

  // Param structs pair each parameter with its size.
  struct naa_param_t *all_params = (naa_param_t *)calloc(params_amount, sizeof(struct naa_param_t));
  for (int i = 0; i < params_amount; i++) {
    all_params[i].addr = (void *)params[i];
    all_params[i].size = param_sizes[i];
  }

  // Separate input and output parameter lists. The third field marks a
  // single-send region, sent only with the first RPC (e.g. config data).
  int input_amount = 4;
  struct naa_param_t input_params[] = {
      {(void *)params[0], param_sizes[0], false},  // single-send flag
      {(void *)params[1], param_sizes[1], false},
      {(void *)params[2], param_sizes[2], false},
      {(void *)params[3], param_sizes[3], false}};

  int output_amount = 3;
  struct naa_param_t output_params[] = {{(void *)params[0], param_sizes[0], false},
                                        {(void *)params[1], param_sizes[1], false},
                                        {(void *)params[3], param_sizes[3], false}};

  // Establish the connection with the NAA.
  ulog_info("-- Setting Up Connection --\n");
  if (naa_create(FNCODE, input_params, input_amount, output_params, output_amount, handle)) {
    ulog_error("Error during naa_create. Exiting.\n");
    return -1;
  };

  // Repeat RPC N_INVOKES times.
  for (int i = 0; i < N_INVOKES; i++) {
    // Call the RPC on the NAA.
    ulog_info("-- RPC Invocation #%d --\n", i + 1);
    if (naa_invoke(handle)) {
      ulog_error("Error durning naa_invoke. Exiting.\n");
      return -1;
    }

    struct naa_status status;
    if (naa_wait(handle, &status)) {
      ulog_error("Error occurred during naa_wait. Exiting.\n");
      return -1;
    }
    ulog_info("Bytes received: %zu, User immediate: %d\n", status.bytes_received,
              status.user_immediate);
  }
  // Tear down the connection.
  ulog_info("-- Cleaning Up --\n");
  naa_finalize(handle);

  // For the simple SWNAA example, the last parameter should be incremented
  // and the others unchanged.
  ulog_info("-- Checking Results --\n");
  for (unsigned char i = 0; i < params_amount; i++) {
    bool success = true;
    unsigned char *data = (unsigned char *)(params[i]);
    for (unsigned int j = 0; j < param_sizes[i]; j++) {
      unsigned char el = data[j];

      if (i == (params_amount - 1)) {
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
