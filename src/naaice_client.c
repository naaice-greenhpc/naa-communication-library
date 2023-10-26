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
 * 12-10-2023
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

/* Main **********************************************************************/

/**
 * Command line arguments:
 *  local-ip, ex. 10.3.10.135
 *  remote-ip, ex. 10.3.10.136
 *  number-of-regions, ex. 1
 *  'region-sizes', ex '1024'
 */
int main(int argc, char *argv[]) {
  
  // Check number of arguments.
  if (argc != 5) {
    fprintf(stderr, "Wrong number of arguments. use: "
      "./naaice_client local-ip remote-ip number-of-regions 'region-sizes' \n "
      "Example: ./naaice_client 10.3.10.135 10.3.10.136 1 '1024'\n");
    return -1;
  };

  // Check against maximum number of memory regions.
  char *ptr;
  long int params_amount = strtol(argv[3], &ptr, 10);
  if(params_amount < 1 || params_amount > 32) {
    fprintf(stderr, "Chosen number of arguments %ld is not supported.\n",
      params_amount);
    return -1;
  }

  // Get sizes of memory regions from command line.
  unsigned int param_sizes[params_amount];

  // First (non-metadata) region.
  char *token = strtok(argv[4], " ");
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
    params[i] = memset(params[i], i, param_sizes[i]);
  }

  // Communication context struct. 
  // This will hold all information necessary for the connection.
  struct naaice_communication_context *comm_ctx = NULL;

  // Initialize the communication context struct.
  if (naaice_init_communication_context(&comm_ctx, param_sizes, params,
    params_amount, FNCODE, argv[1], argv[2], CONNECTION_PORT)) { return -1; }

  // First, handle connection setup.
  if (naaice_setup_connection(comm_ctx)) { return -1; }

  // Then, register the memory regions.
  if (naaice_register_mrs(comm_ctx)) { return -1; }

  // Set metadata (i.e. return address).
  // For our example, the return parameter is the last one.
  if (naaice_set_metadata(comm_ctx, (uintptr_t) comm_ctx->mr_local_data[params_amount-1].addr)){//params[params_amount-1])) {
    return -1; }

  // Do the memory region setup protocol, and then post a recieve for the
  // FPGA to post back its response.
  if (naaice_init_mrsp(comm_ctx)) { return -1; }

  // TODO: Consider reimplementing the communication state machine.

  // Loop handling data transmission until RPC and communication is complete.
  if (naaice_poll_cq_blocking(comm_ctx)) { return -1; }

  // Disconnect and cleanup.
  if (naaice_disconnect_and_cleanup(comm_ctx)) { return -1; }

  // At this point, we can check the data for correctness.

  return 0;
}