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

#define MAX_CONNECTIONS 8


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

// This function handles the allocation and initialization of a new
// communication context. Finds a free spot in the array of communication
// contexts and allocates a new one there.
// Returns -1 if no space for a new connection, or an error while initializing
// one, and 0 otherwise.
int create_new_connection(
  struct naaice_communication_context ***communication_contexts, const char *local_ip) {

  for (int i = 0; i < MAX_CONNECTIONS; i++) {
    if ((*communication_contexts)[i] == NULL) {
      printf("++ Initializing Communication Context ++\n");
      int result = naaice_swnaa_init_communication_context(&((*communication_contexts)[i]), local_ip, CONNECTION_PORT);
      return result;
    }
  }

  // If we've reached this point, there was no space for a new connection.
  // Return an error.
  return -1;
}

// This function handles all actions necessary on an NAA connection in a
// nonblocking fashion. This includes handling connection setup, communication,
// and disconnection.
// Calling this function in a loop should be sufficient to handle an entire
// session for one connection.
int handle_connection(struct naaice_communication_context *comm_ctx) {

  //printf("in handle_connection\n");

  // Switch based on state.
  switch (comm_ctx->state) {

    case (INIT): {
      //printf("INIT\n");
      // Handle any connection events.
      return naaice_swnaa_poll_and_handle_connection_event(comm_ctx);
    }
    case (READY): {
      //printf("READY\n");
      // Handle any connection events.
      return naaice_swnaa_poll_and_handle_connection_event(comm_ctx);
    }
    case (CONNECTED): {
      //printf("CONNECTED\n");
      // Initialize the MRSP.
      printf("++ Doing MRSP ++\n");
      return naaice_swnaa_init_mrsp(comm_ctx);
    }
    case (MRSP_RECEIVING): {
      //printf("MRSP_RECEIVING\n");
      // Handle any MRSP events.
      return naaice_swnaa_poll_cq_nonblocking(comm_ctx);
    }
    case (MRSP_SENDING): {
      //printf("MRSP_SENDING\n");
      // Handle any MRSP events.
      return naaice_swnaa_poll_cq_nonblocking(comm_ctx);
    }
    case (MRSP_DONE): { // ENCAPSULATE
      //printf("MRSP_DONE\n");
      // Increment number of RPC calls.
      comm_ctx->no_rpc_calls++;

      // Check if connection event is available.
      int fd_flags = fcntl(comm_ctx->ev_channel->fd, F_GETFL);
      if (fcntl(comm_ctx->ev_channel->fd, F_SETFL, fd_flags | O_NONBLOCK) < 0) {
        fprintf(stderr, "Failed to change file descriptor of event channel.\n");
        return -1;
      }

      struct pollfd my_pollfd;
      int ms_timeout = 1;
      // Poll the completion channel, returning with flag unchanged if nothing
      // is received.
      my_pollfd.fd = comm_ctx->ev_channel->fd;
      my_pollfd.events = POLLIN;
      my_pollfd.revents = 0;

      // Nonblocking: if poll times out, just return.
      int poll_result = poll(&my_pollfd, 1, ms_timeout);
      if (poll_result < 0) {
        fprintf(stderr, "Error occured when polling completion channel.\n");
        return -1;
      } else if (poll_result > 0) {
        int result = naaice_swnaa_poll_and_handle_connection_event(comm_ctx);
        return result;
      }

      // Else continue, there was no event.

      // Post a receive for the data.
      printf("++ Receiving Data Transfer ++\n");
      return naaice_swnaa_post_recv_data(comm_ctx);
    }

    case (DATA_RECEIVING): {
      //printf("DATA_RECEIVING\n");
      // Handle any events. 
      return naaice_swnaa_poll_cq_nonblocking(comm_ctx);
    }
    case (CALCULATING): {
      //printf("CALCULATING\n");
      // Now that all data has arrived, perform the RPC.
      printf("++ Doing RPC ++\n");
      printf("Function Code: %d\n", comm_ctx->fncode);
      uint8_t errorcode = do_procedure(comm_ctx);
      comm_ctx->state = DATA_SENDING;

      // Then write data back.
      return naaice_swnaa_write_data(comm_ctx, errorcode);
    }

    case (DATA_SENDING): {
      //printf("DATA_SENDING\n");
      // Handle any events. 
      return naaice_swnaa_poll_cq_nonblocking(comm_ctx);
    }
    case (FINISHED): {
      //printf("FINISHED\n");
      printf("++ Invoke Finished ++\n");
      // If one RPC is finished, return to waiting for another invoke request.
      //comm_ctx->state = DATA_RECEIVING;
      comm_ctx->state = MRSP_DONE;
      return 0;
    }
    case (DISCONNECTED): {
      printf("DISCONNECTED\n");
      return -1;
    }
    case (ERROR): {
      printf("ERROR\n");
      return -1;
    }
    default: {
      printf("UNHANDLED STATE\n");
      return -1;
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
  printf("++ Handling Command Line Arguments ++\n");
  if ((argc != 1) && (argc != 2)) {
    fprintf(stderr, "Wrong number of arguments. use: "
      "./naaice_server [local-ip]\n"
      "Example: ./naaice_server 10.3.10.134\n");
    return -1;
  }

  // Get local IP argument, if provided.
  char empty_str[1] = {'\0'}; //"";
  char *local_ip = empty_str;
  if (argc > 1) {
    local_ip = argv[1];
  }

  // Communication context structs. 
  // These will hold all information necessary for the connection.
  printf("++ Making list of Communication Contexts ++\n");
  struct naaice_communication_context **communication_contexts =
    (struct naaice_communication_context **)
    calloc(MAX_CONNECTIONS, sizeof(struct naaice_communication_context*));
  //memset(communication_contexts, 0, MAX_CONNECTIONS);

  // Main loop: wait for new connections and handle activity on all
  // active connections.
  uint8_t num_active_connections = 0;
  while (true) {

    // If no active connections, initialize one.
    if (num_active_connections == 0) {
      // Initialize the communication context struct.
      // Abort if there is an error initializing a new connection.
      int result = create_new_connection(&communication_contexts, local_ip);
      if (result) { return result; }
      num_active_connections++;
    }

    // For any active connections...
    for (int i = 0; i < MAX_CONNECTIONS; i++) {

      struct naaice_communication_context *comm_ctx = communication_contexts[i];
      if (comm_ctx != NULL) {

        // Get the initial state of the connection.
        enum naaice_communication_state initial_state = comm_ctx->state;

        // Handle any activity on the connection.
        int handle_result = handle_connection(comm_ctx);

        // Get the connection state after handling any events.
        enum naaice_communication_state final_state = comm_ctx->state;

        // If any error occured, disconnect.
        if ((final_state == ERROR) || (handle_result)) {
          printf("ERROR or handle_result\n");

          if (final_state != DISCONNECTED) {
            printf("++ Cleaning Up ++\n");
            int disconnect_result = naaice_swnaa_disconnect_and_cleanup(comm_ctx);
            if (disconnect_result) { return -1; }
            final_state = DISCONNECTED;
          }
        }

        // If connection terminated, free up the communication context.
        if (final_state == DISCONNECTED) {
          free(comm_ctx);
          communication_contexts[i] = NULL;
          num_active_connections--;
        }

        // If connection established, allocate a new communication context
        // to wait for another client.
        if ((initial_state < CONNECTED) && (final_state >= CONNECTED)) {
          
          // Initialize the communication context struct.
          // Abort if there is an error initializing a new connection.
          int result = create_new_connection(&communication_contexts, local_ip);
          if (result) { return result; }
          num_active_connections++;
        }
      }
    }
  }

  return 0;
}
