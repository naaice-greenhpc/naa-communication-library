/**************************************************************************/ /**
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
* 12-10-2023
*
*****************************************************************************/

/* Dependencies **************************************************************/
#include <naaice_ap2.h>

// Struct to hold information about a single (input or output) parameter.

int main(){
  // Struct to hold information about a NAA session.
  struct naa_handle *handle;
  handle = calloc(1,sizeof(struct naa_handle));
  if(!handle){
    fprintf(stderr,"Failed to create naa handle, exiting\n");

  } 
  //Allocate Variables 
  float *concentrations,*alphax,*alphay,*results;
  posix_memalign((void **)&(concentrations),
                 sysconf(_SC_PAGESIZE), sizeof(float)*1024);
  if (!concentrations) {
    fprintf(stderr, "Failed to allocate concetrations buffer.\n");
    return -1;
  }
  posix_memalign((void **)&(alphax), sysconf(_SC_PAGESIZE),
                 sizeof(float) * 1024);
  if (!alphax) {
    fprintf(stderr, "Failed to allocate alphax buffer.\n");
    return -1;
  }
  posix_memalign((void **)&(alphay), sysconf(_SC_PAGESIZE),
                 sizeof(float) * 1024);
  if (!alphay) {
    fprintf(stderr, "Failed to allocate alphay buffer.\n");
    return -1;
  }
  posix_memalign((void **)&(results), sysconf(_SC_PAGESIZE),
                 sizeof(float) * 1024);
  if (!results) {
    fprintf(stderr, "Failed to allocate alphay buffer.\n");
    return -1;
  }
unsigned int param_nums=4;
int N = 1024;
struct naa_param_t params[] = {{(void *)concentrations, N * sizeof(float)},
                               {(void *)alphax, N * sizeof(float)},
                               {(void *)alphay, N * sizeof(float)},
                               {(void *)results, N * sizeof(float)}};

/*params[0]->addr = concentrations;
params[0]->size = 1024;
params[1]->addr = alphax;
params[1]->size = 1024;
params[2]->addr = alphay;
params[2]->size = 1024;
params[3]->addr = results;
params[3]->size = 1024;*/
if (naa_create(1, params, param_nums, handle)) {
  fprintf(stderr, "Error durning naa_create. Exiting\n");
  exit(EXIT_FAILURE);
};
unsigned int in_params_nums = 3;
struct naa_param_t input_params[] = {{(void *)concentrations, N * sizeof(float)},
                               {(void *)alphax, N * sizeof(float)},
                               {(void *)alphay, N * sizeof(float)}};
unsigned int out_params_nums = 1;
struct naa_param_t output_params[] = {{(void *)results, N * sizeof(float)}};

memcpy(&params[0], &input_params[0],
       sizeof(struct naa_param_t) * in_params_nums);
memcpy(&params[3],&output_params,sizeof(struct naa_param_t));
naa_invoke(input_params, in_params_nums, *output_params, handle);
bool flag=false;
struct naa_status status;

while(!flag){
  naa_test(handle, &flag, &status);
}
naa_finalize(handle);
exit(EXIT_SUCCESS);
}