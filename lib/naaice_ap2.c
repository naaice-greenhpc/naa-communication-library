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
 * naaice_ap2.c
 *
 * Implementations for functions in naaice_ap2.c.
 * 
 * Florian Mikolajczak, florian.mikolajczak@uni-potsdam.de
 * Dylan Everingham, everingham@zib.de
 * 
 * 07-02-2024
 * 
 *****************************************************************************/

/* Dependencies **************************************************************/

#include "naaice_ap2.h"
#include "ulog.h"

/* Constants *****************************************************************/

//TODO: HHI prefers as little MRs as possible/usable by the user.
#define MAX_PARAMS 32
#define TIMEOUT_INVOKE 100 	// All timeouts given in ms.
#define TIMEOUT_TEST 100
#define TIMEOUT_WAIT 100
#define CONNECTION_PORT 12345

// TODO: Provided by RMS in the future.
// UP IPs.
static const char *LOCAL_IP = "10.3.10.41";
static const char *REMOTE_IP = "10.3.10.42";

// ZIB IPs.
//static const char *LOCAL_IP = ""; // Indicate that we don't provide the
																	// optional local ip argument with an
																	// empty string.
//static const char *REMOTE_IP = "10.32.56.10";

// Struct used to hold configuration info about NAA hardware.
typedef struct naa_hardware_config {
	size_t mem_size; // in bytes
} naa_hardware_config;

// Structs used to hold configuration info about NAA routines.
#define N_ROUTINES_PER_NAA 1
typedef struct naa_routine_config {
	unsigned int id;
	unsigned int n_params;
} naa_routine_config;
typedef struct naa_routineset_config {
	unsigned int n_routines;
	struct naa_routine_config routines[N_ROUTINES_PER_NAA];
} naa_routineset_config;

// Dummy configuration file values.
// Info hardcoded for now, but should be read from config file in the future.
#define N_AVAILABLE_NAAS 1
#define N_NAA_ROUTINESETS 1
#define N_VECTORMATH_ROUTINES 1
const struct naa_routine_config naa_routine_vectoradd =
	{.id = 0, .n_params = 3};
const struct naa_routineset_config naa_routineset_vectormath =
	{.n_routines = N_VECTORMATH_ROUTINES, .routines = {naa_routine_vectoradd}};
const struct naa_routineset_config naa_routinesets[N_NAA_ROUTINESETS] = 
	{naa_routineset_vectormath};


/* Helper Functions **********************************************************/

// Function to retrieve network parameters.
// Should eventually read from config file (with help from RMS / memory
// management service?).
int get_network_params(char *local_ip, char *remote_ip, uint16_t *port) {
	// For now, dummy implementation:
	// Just return some locally defined constants.
	strcpy(local_ip, LOCAL_IP);
	strcpy(remote_ip, REMOTE_IP);
	*port = CONNECTION_PORT;
	return 0;
}

// Function to check validity of parameters against configuration values.
int check_params(unsigned int function_code, 
	__attribute__((unused)) naa_param_t *params,
	unsigned int params_amount) {

	// Check validity of parameters based on dummy config values.
	// TODO: Replace this with a file read / RMS thing.
	// TODO: Check param sizes as well?
	bool params_valid = false;
	for (unsigned int i = 0; i < N_NAA_ROUTINESETS; i++) {
		unsigned int n_routines = naa_routinesets[i].n_routines;
		for (unsigned int j = 0; j < n_routines; j++) {
			if (naa_routinesets[i].routines[j].id == function_code) {
				if (naa_routinesets[i].routines[j].n_params == params_amount) {
					params_valid = true;
				}
				else {
					// Number of params is wrong.
					log_error( "Invalid number of parameters provided.\n");
					return -1;
				}
			}
		}
	}

	// If flag is still false, this means that function_code is invalid.
	if (!params_valid) {
		log_error("Invalid function code provided.\n");
		return -1;
	} 

	// Otherwise return with a success.
	return 0;
}


/* Public Function Implementations *******************************************/

int naa_create(unsigned int function_code, 
  naa_param_t *input_params, unsigned int input_amount,
  naa_param_t *output_params, unsigned int output_amount,
  naa_handle *handle) {

  //check if we have regions that are input and ouput	
  bool output_as_input[output_amount];
  for (int i = 0; i < output_amount; i++) {
    output_as_input[i] = false;
  }

        // Clear the naa_handle.
	memset(handle, 0, sizeof(naa_handle));

	// Add the function code to the handle.
	handle->function_code = function_code;

	// TODO: Get rotine config info from file here.

	// Check validity of parameters.
	// TODO: Enable checking again
	// FM: Check disabled for now
	//if (check_params(function_code, params, params_amount)) { return -1; }

	// Get port and IP from RMS / configuration file.
	// TODO: Handle errors from this.
	char local_ip[40];
	char remote_ip[40];
	uint16_t port = 0;
	get_network_params(local_ip, remote_ip, &port);

	// Convert the params into the representation expected by the API layer
	// (i.e. without the naa_param_t type).
	size_t params_amount = input_amount + output_amount;
  for (unsigned int i = 0; i < output_amount; i++){
		for (unsigned int j = 0; j < input_amount; j++) {
      if ((char *)output_params[i].addr == (char *)input_params[j].addr) {
        // we have the same input as output, but dont want to reallocate and
        // reregister
        params_amount--;
        output_as_input[i] = true;
				break;
      }
		}
  }

  char *param_addrs[params_amount];
	size_t param_sizes[params_amount];
	for (unsigned int i = 0; i < input_amount; i++) {
		param_addrs[i] = (char*) input_params[i].addr;
		param_sizes[i] = input_params[i].size;
	}

	unsigned idx = 0;
	for (unsigned int i = 0; i < output_amount; i++) {
		if (output_as_input[i] == false) {
      param_addrs[idx + input_amount] = (char *)output_params[i].addr;
      param_sizes[idx + input_amount] = output_params[i].size;
      idx++;
		}
	}

	// Initialize the communication context.
	if (naaice_init_communication_context(&(handle->comm_ctx), 0,
										  param_sizes, param_addrs, params_amount, 0, 0, function_code,
										  local_ip, remote_ip, port)) {
		return -1;
	}

	// Set immediate value which will be sent later as part of the data transfer.
  uint8_t *imm_bytes = (uint8_t*) calloc(3, sizeof(uint8_t));
  if (naaice_set_immediate(handle->comm_ctx, imm_bytes)) { return -1; }

	// Setup the connection to the NAA.
  if (naaice_setup_connection(handle->comm_ctx)) { return -1; }

  // Set input and output parameters.
  // For each input and output parameter pointer, check that it refers to one
  // of the parameters already saved in the communication context.
  // If it is, set it as an input or output parameter appropriately.
  // Otherwise return with an error.
  // Also check whether the constant value of naa_params_t has been set to
  // true. If yes, then set memory region to singlesend.
  for (unsigned int i = 0; i < input_amount; i++) {

    bool param_exists = false;
    for (int j = 0; j < handle->comm_ctx->no_local_mrs; j++) {

      if (input_params[i].addr ==
          (void *)handle->comm_ctx->mr_local_data[j].addr) {

        param_exists = true;
        if (input_params[i].single_send == true) {
          if (naaice_set_singlesend_mr(handle->comm_ctx, j)) {
            log_error("Error on setting constant memory regions (single "
                            "send regions).\n");
            return -1;
          };
        }
        if (naaice_set_input_mr(handle->comm_ctx, j)) {
          return -1;
        }
        break;
      }
    }
    if (!param_exists) {
      log_error( "Requested input parameter which was not previously "
                      "passed to naaice_init_communication_context.\n");
      return -1;
    }
	}
	for (unsigned int i = 0; i < output_amount; i++) {

		bool param_exists = false;
		for (int j = 0; j < handle->comm_ctx->no_local_mrs; j++) {

			if (output_params[i].addr == (void*) handle->comm_ctx->mr_local_data[j].addr) {

				param_exists = true;
				if(output_params[i].single_send == true){
					if(naaice_set_singlesend_mr(handle->comm_ctx, j)){
						log_error( "Error on setting constant memory regions (single send regions).\n");
						return -1;
					};
				}
				if (naaice_set_output_mr(handle->comm_ctx, j)) { return -1; }
				break;
			}
		}
		if (!param_exists) {
			log_error( "Requested output parameter which was not previously "
				"passed to naaice_init_communication_context.\n");
			return -1;
		}
	}

  // Register the memory regions.
  if (naaice_register_mrs(handle->comm_ctx)) { return -1; }

  // FM: Moved MRSP from naa_invoke to here. It's only done once
  // Do the memory region setup protocol.
  if (naaice_do_mrsp(handle->comm_ctx)) {
    return -1;
  }

  return 0;
}
// FM: This is obsolete right? we implemented sending only specific regions
// TODO: Actually use input_params to set which data gets transferred.
// Make input types the same for input/output? 
int naa_invoke(naa_handle *handle) {

  // Initialize data transfer to the NAA.
  if (naaice_init_data_transfer(handle->comm_ctx)) { return -1; }

	return 0;
}

int naa_test(naa_handle *handle, bool *flag,
	naa_status *status) {

	// Check for NAA completion. If this returns -1, an error occured.
	if (naaice_poll_cq_nonblocking(handle->comm_ctx)) { return -1; }

	// Update completion flag.
	if (handle->comm_ctx->state >= NAAICE_FINISHED){
		*flag = true;
	}
	else {
		*flag = false;
	}
                                      
	// Update the status struct,
	status->state = handle->comm_ctx->state;
	status->naa_error = (enum naa_error) handle->comm_ctx->naa_returncode;
	status->bytes_received = handle->comm_ctx->bytes_received;
	return 0;
}

// TODO: When blocking method is implemented, change this to do blocking
int naa_wait(naa_handle *handle,
	naa_status *status) {
  if(naaice_poll_cq_blocking(handle->comm_ctx)){return -1;}
    /*	bool flag = false
	      while (!flag) {
        if (naa_test(handle, &flag, status)) {
          fprintf(stderr, "Error occurred during naa_test. Exiting.\n");
          return -1;
        }
      }*/
  status->state = handle->comm_ctx->state;
  status->naa_error = (enum naa_error)handle->comm_ctx->naa_returncode;
  status->bytes_received = handle->comm_ctx->bytes_received;
  return 0;
  }

int naa_finalize(naa_handle *handle) {

	// Disconnect and clean up.
	return naaice_disconnect_and_cleanup(handle->comm_ctx);
	// TODO: clean up handle memory.
}
