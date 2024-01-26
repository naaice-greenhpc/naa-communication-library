/*************************************************************************/ /**
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
* 26-01-2024
*
******************************************************************************/

/* Dependencies **************************************************************/

#include <naaice_ap2.h>


/* Constants *****************************************************************/

// Function code specifying RPC.
#define FNCODE 1

// Number of times to repeat the RPC.
#define N_INVOKES 3


/* Main **********************************************************************/

/**
 * Command line arguments:
 *  number-of-regions, ex. 1
 *  'region-sizes', ex '64, 128'
 */
int main(int argc, char *argv[]) {

  printf("-- Handling Command Line Arguments --\n");
  
  // Check number of arguments.
  if (argc != 3) {
    fprintf(stderr, "Wrong number of arguments. use: "
      "./naaice_client_ap2 number-of-regions 'region-sizes'\n"
      "Example: ./naaice_client_ap2 2 '64, 128'\n");
    return -1;
  };

  // Check against maximum number of memory regions.
  char *ptr;
  long int params_amount = strtol(argv[1], &ptr, 10);
  if(params_amount < 1 || params_amount > MAX_MRS) {
    fprintf(stderr, "Chosen number of arguments %ld is not supported.\n",
      params_amount);
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
  
  // Handle struct holds all information about a NAA session.
  struct naa_handle *handle = calloc(1,sizeof(struct naa_handle));
  if (!handle) {
    fprintf(stderr,"Failed to create naa handle. Exiting.\n");
    return -1;
  }

  // Param structs encapsulate parameters and their sizes.
  struct naa_param_t *all_params = calloc(
    params_amount, sizeof(struct naa_param_t));
  for (int i = 0; i < params_amount; i++) {
    all_params[i].addr = (void*) params[i];
    all_params[i].size = param_sizes[i];
  }

  // These structs hold separately input and output parameters.
  // As an example, specify the first two parameters as inputs and the second
  // parameter as an output.
  int n_input_params = 2;
  struct naa_param_t input_params[] = {
    {(void *) params[0], param_sizes[0]},
    {(void *) params[1], param_sizes[1]}
  };

  int n_output_params = 1;
  struct naa_param_t output_params[] = {
    {(void *) params[1], param_sizes[1]}
  };

  // naa_create: establishes connection with NAA.
  printf("-- Setting Up Connection --\n");
  if (naa_create(FNCODE, all_params, params_amount, handle)) {
    fprintf(stderr, "Error durning naa_create. Exiting.\n");
    return -1;
  };

  // Repeat RPC N_INVOKES times.
  for (int i = 0; i < N_INVOKES; i++) {

    // naa_invoke: call RPC on NAA.
    printf("-- RPC Invocation #%d --\n", i+1);
    if (naa_invoke(input_params, n_input_params,
      output_params, n_output_params, handle)) {
      fprintf(stderr, "Error durning naa_invoke. Exiting.\n");
      return -1;
    }

    // naa_test: keep tabs on the status of the RPC.
    bool flag = false;
    struct naa_status status;
    while (!flag) {
      if (naa_test(handle, &flag, &status)) {
        fprintf(stderr, "Error occured during naa_test. Exiting.\n");
        return -1;
      }
    }
  }

  // naa_finalize: clean up connection.
  printf("-- Cleaning Up --\n");
  naa_finalize(handle);

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