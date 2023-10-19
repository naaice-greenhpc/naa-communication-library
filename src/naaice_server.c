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
 * naaice_server.c
 *
 * Application implementing a basic use case of the AP1 NAAICE communication
 * layer.
 * For use on a loopback test setup, in conjunction with naaice_client.c.
 * 
 * The actual logic of the NAA procedure can be changed by putting whatever
 * you like in the implementation of do_procedure().
 * 
 * Florian Mikolajczak, florian.mikolajczak@uni-potsdam.de
 * Dylan Everingham, everingham@zib.de
 * 
 * 12-10-2023
 * 
 *****************************************************************************/

/* Dependencies **************************************************************/

#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <naaice.h>
#include <naaice_swnaa.h>


/* Constants *****************************************************************/

#define CONNECTION_PORT 12345

/* Implementation of NAA Procedure *******************************************/

// The routine should return 0 if successful, or some nonzero number if not.
// This nonzero error code is sent to the host in the case of an error.
uint8_t do_procedure(struct naaice_communication_context *comm_ctx) {

  // Example:
  // Assume all data in the memory regions is arrays of chars.
  // Increment all chars in all memory regions by one.

  for (int i = 0; i < comm_ctx->no_local_mrs; i++) {

    // Get pointer to data.
    char *data = (char*) comm_ctx->mr_local_data[i].addr;

    for(int j = 0; j < comm_ctx->mr_local_data[i].size; j++) {

      // Increment a char.
      data[j]++;
    }
  }

  return 0;
}

/* Main **********************************************************************/

/**
 * Command line arguments:
 *  None. Port is hardcoded.
 */
int main(int argc, __attribute__((unused)) char *argv[]) {

  // Handle command line arguments.
  if (argc != 1) {
    fprintf(stderr,
            "Server should be called without arguments.\n");
    return -1;
  }

  // Communication context struct. 
  // This will hold all information necessary for the connection.
  struct naaice_communication_context *comm_ctx = NULL;

  // Initialize the communication context struct.
  if(naaice_swnaa_init_communication_context(&comm_ctx, CONNECTION_PORT)) {
    return -1; }

  // First, handle connection setup.
  // This step functions exactly the same as on the host side.
  if (naaice_swnaa_setup_connection(comm_ctx)) { return -1; }

  // Start listening for MRSP messages from the host.
  if (naaice_swnaa_init_mrsp(comm_ctx)) { return -1; }

  // Then, loop, waiting for the MRSP messages from the host, and handling
  // them when they arrive.
  if (naaice_swnaa_poll_cq_blocking_mrsp(comm_ctx)) { return -1; }

  // Next, loop, waiting for transmissions of data (i.e. metadata + RPC
  // parameters) from the host, handling them as they arrive.
  if (naaice_swnaa_poll_cq_blocking_data(comm_ctx)) { return -1; }

  // Now that all data has arrived, perform the RPC.
  uint8_t errorcode = do_procedure(comm_ctx);

  // Finally, write back the results to the host.
  if (naaice_swnaa_write_data(comm_ctx, errorcode)) { return -1; }

  // Disconnect and clean up.
  if (naaice_swnaa_disconnect_and_cleanup(comm_ctx)) { return -1; }

  return 0;
}
