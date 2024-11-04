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
 * 26-01-2024
 * 
 *****************************************************************************/

/* Dependencies **************************************************************/

#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <naaice.h>
#include <debug.h>
#include <naaice_swnaa.h>


/* Constants *****************************************************************/

#define CONNECTION_PORT 12345


/* Implementation of NAA Procedure *******************************************/

// The routine should return 0 if successful, or some nonzero number if not.
// This nonzero error code is sent to the host in the case of an error.
uint8_t do_procedure(struct naaice_communication_context *comm_ctx) {

  printf("in do_procedure\n");

  // Can switch on function code.
  // Here, do nothing if code is 0. Otherwise do something.
  if (comm_ctx->fncode) {

    // Example:
    // Assume all data in the memory regions is arrays of chars.
    // Increment all chars in all memory regions by one.

    for (unsigned int i = 0; i < comm_ctx->no_local_mrs; i++) {

      // Get pointer to data.
      unsigned char *data = (unsigned char*) comm_ctx->mr_local_data[i].addr;

      for(unsigned int j = 0; j < comm_ctx->mr_local_data[i].size; j++) {

        // Increment a char.
        //printf("data value (old/new): %u", data[j]);
        data[j]++;
        //printf("/%u\n", data[j]);
      }
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
  printf("-- Handling Command Line Arguments --\n");
  if (argc != 1) {
    fprintf(stderr,
            "Server should be called without arguments.\n");
    return -1;
  }

  // Communication context struct. 
  // This will hold all information necessary for the connection.
  printf("-- Initializing Communication Context --\n");
  struct naaice_communication_context *comm_ctx = NULL;

  // Initialize the communication context struct.
  if(naaice_swnaa_init_communication_context(&comm_ctx, CONNECTION_PORT)) {
    return -1; }

  // Now, handle connection setup.
  printf("-- Setting Up Connection --\n");
  if (naaice_swnaa_setup_connection(comm_ctx)) { return -1; }

  // Receive MRSP message from the host.
  printf("-- Doing MRSP --\n");
  if (naaice_swnaa_do_mrsp(comm_ctx)) { return -1; }

  while (comm_ctx->state >= MRSP_DONE) {
    
    // Receive data transfer from host.
    printf("-- Receiving Data Transfer --\n");
    if (naaice_swnaa_receive_data_transfer(comm_ctx)) { return -1; }
    if (comm_ctx->state < MRSP_DONE || comm_ctx->state == FINISHED){
      break;
    }

    // Now that all data has arrived, perform the RPC.
    printf("-- Doing RPC --\n");
    printf("Function Code: %d\n", comm_ctx->fncode);
    uint8_t errorcode = do_procedure(comm_ctx);

    // Finally, write back the results to the host.
    printf("-- Writing Back Data --\n");
    if (naaice_swnaa_do_data_transfer(comm_ctx, errorcode)) { return -1; }

    if (naaice_swnaa_poll_and_handle_connection_event(comm_ctx)<0) { return -1; }
    else if(comm_ctx->state==FINISHED){
      break;
    }
  }

  // Disconnect and clean up.
  printf("-- Cleaning Up --\n");
  if (naaice_swnaa_disconnect_and_cleanup(comm_ctx)) { return -1; }

  return 0;
}
