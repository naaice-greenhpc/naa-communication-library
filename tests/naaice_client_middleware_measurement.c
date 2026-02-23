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

#include "ulog.h"
#include <naaice_ap2.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define CONNECTION_PORT 12345
#define FNCODE 3

#define N_INVOKES 1

double get_timestamp() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (double)(tv.tv_sec * (uint64_t)1000000 + tv.tv_usec) / 1.0e6;
}

int main(int argc, char *argv[]) {
#ifndef ULOG_BUILD_DISABLED
  ulog_output_level_set_all(LOG_LEVEL);
#endif
  if (argc != 3) {
    fprintf(stderr, "Wrong number of arguments. use: "
                    "./naaice_client region-sizes logfile\n"
                    "Example: ./naaice_client 1024 logfile\n");
    return -1;
  };

  size_t param_sizes[1];
  param_sizes[0] = atoi(argv[1]);

  char *params[1];
  params[0] = (char *)malloc(param_sizes[0] * sizeof(char));
  if (params[0] == NULL) {
    fprintf(stderr, "Failed to allocate memory for parameters.\n");
    return -1;
  }

  struct naa_handle *handle =
      (naa_handle *)calloc(1, sizeof(struct naa_handle));
  if (!handle) {
    ulog_error("Failed to create naa handle. Exiting.\n");
    return -1;
  }

  naa_status status;

  struct naa_param_t input_params[] = {
      {(void *)params[0], param_sizes[0], false}};

  struct naa_param_t output_params[] = {
      {(void *)params[0], param_sizes[0], false}};

  if (naa_create(FNCODE, input_params, 1, output_params, 1, handle)) {
    return -1;
  };

  double start = get_timestamp();
  if (naa_invoke(handle)) {
    return -1;
  };
  naa_wait(handle, &status);
  double end = get_timestamp();

  naa_finalize(handle);
  free(params[0]);

  FILE *logfile = fopen(argv[2], "a");
  if (logfile == NULL) {
    fprintf(stderr, "Failed to open logfile.\n");
    return -1;
  }
  fprintf(logfile, "%ld,%f\n", param_sizes[0], end - start);
  fclose(logfile);

  return 0;
}