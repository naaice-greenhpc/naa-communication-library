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
 * naaice_client.c
 *
 * Application implementing a basic use case of the AP1 NAAICE communication
 * layer.
 * 
 * For use on a loopback test setup, in conjunction with naaice_server.c.
 * 
 * Florian Mikolajczak, florian.mikolajczak@uni-potsdam.de
 * Dylan Everingham, everingham@zib.de
 * 
 * 26-01-2024
 * 
 *****************************************************************************/

/* Dependencies **************************************************************/

#include <naaice.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>


/* Constants *****************************************************************/

#define CONNECTION_PORT 12345
#define FNCODE 1

// Number of times to repeat the RPC.
#define N_INVOKES 3


/* Main **********************************************************************/

/**
 * Command line arguments:
 *  local-ip, ex. 10.3.10.135 (optional)
 *  remote-ip, ex. 10.3.10.136
 *  number-of-regions, ex. 1
 *  'region-sizes', ex '1024'
 */
int main(int argc, char *argv[]) {

  printf("-- Handling Command Line Arguments --\n");
  
  // Check number of arguments.
  if ((argc != 4) && (argc != 5)) {
    fprintf(stderr, "Wrong number of arguments. use: "
      "./naaice_client [local-ip] remote-ip number-of-regions 'region-sizes'\n"
      "Example: ./naaice_client 10.3.10.134 10.3.10.135 1 '1024'\n");
    return -1;
  };

  // Check if optional local IP argument was provided.
  char empty_str[1] = "";
  int arg_offset = (argc == 4) ? 0 : 1;
  char *local_ip = (argc == 4) ? empty_str : argv[1];

  // Check against maximum number of memory regions.
  char *ptr;
  long int params_amount = strtol(argv[2+arg_offset], &ptr, 10);
  if(params_amount < 1 || params_amount > MAX_MRS) {
    fprintf(stderr, "Chosen number of arguments %ld is not supported.\n",
      params_amount);
    return -1;
  }

  // Get sizes of memory regions from command line.
  size_t param_sizes[params_amount];

  // First region.
  char *token = strtok(argv[3+arg_offset], " ");
  param_sizes[0] = atoi(token);

  // If more sizes provided than the specified number of regions, exit.
  int i = 0;
  while (i <= params_amount) {
    i++;
    token = strtok(NULL, " ");
    if(token == NULL) {
      if(i < params_amount) {
        fprintf(stderr,"Higher number of memory regions requested "
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

    params[i] = malloc(param_sizes[i] * sizeof(char));
    if (params[i] == NULL) {
      fprintf(stderr, "Failed to allocate memory for parameters.\n");
      return -1;
    }

    params[i] = memset(params[i], i, param_sizes[i]);
  }

  // Communication context struct. 
  // This will hold all information necessary for the connection.
  printf("-- Initializing Communication Context --\n");
  struct naaice_communication_context *comm_ctx = NULL;

  // Initialize the communication context struct.
  if (naaice_init_communication_context(&comm_ctx, param_sizes, params,
    params_amount, FNCODE, local_ip, argv[1+arg_offset], CONNECTION_PORT)) {
      return -1;
  }

  // Now, handle connection setup.
  printf("-- Setting Up Connection --\n");
  if (naaice_setup_connection(comm_ctx)) { return -1; }

  // Specify input and output parameters.
  // As an example, specify the first two parameters as inputs and the second
  // parameter as an output.
  printf("-- Specifying Input and Output Memory Regions --\n");
  if (naaice_set_input_mr(comm_ctx, 0)) { return -1; }
  if (naaice_set_input_mr(comm_ctx, 1)) { return -1; }
  if (naaice_set_output_mr(comm_ctx, 1)) { return -1; }

  // Then, register the memory regions with IBV.
  printf("-- Registering Memory Regions with IBV --\n");
  if (naaice_register_mrs(comm_ctx)) { return -1; }

  // Also configure internal memory regions.
  // As an example, specify a single internal memory region with address 0
  // and size 32.
  printf("-- Specifying NAA Internal Memory Regions --\n");
  uintptr_t internal_addrs[1] = {0};
  size_t internal_sizes[1] = {32};
  if (naaice_set_internal_mrs(comm_ctx, 1,
        internal_addrs, internal_sizes)) { return -1; }

  // Do the memory region setup protocol.
  printf("-- Doing MRSP --\n");
  if (naaice_do_mrsp(comm_ctx)) { return -1; }

  // Do the data transfer, including commnication of parameters to NAA,
  // waiting for calculation to complete, and receiving return parameter
  // back from NAA.
  // Repeat RPC N_INVOKES times.
  printf("-- Doing Data Transfer --\n");
  for (int i = 0; i < N_INVOKES; i++) {

    printf("-- RPC Invocation #%d --\n", i+1);
    if (naaice_do_data_transfer(comm_ctx)) { return -1; }
  }

  // Disconnect and cleanup.
  printf("-- Cleaning Up --\n");
  if (naaice_disconnect_and_cleanup(comm_ctx)) { return -1; }

  // At this point, we can check the data for correctness.
  // For the simple SWNAA example, we expect all values in the last parameter
  // to have been incremented, and the other parameters to be unchanged.
  printf("-- Checking Results --\n");
  for (unsigned char i = 0; i < params_amount; i++) {

    bool success = true;
    unsigned char *data = (unsigned char *)(params[i]);
    for(unsigned int j = 0; j < param_sizes[i]; j++) {

      unsigned char el = data[j];

      if (i == (params_amount - 1)) {
        if (el != (i + N_INVOKES)) {
          success = false;
        }
      }
      else {
        if (el != i) { success = false; }
      }
    }

    printf("Parameter %u: first element: %u. Success? %s\n",
          i, data[0], success ? "yes" : "no");
  }

  return 0;
}