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

/* Dependencies **************************************************************/

#include "ulog.h"
#include <naaice.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define CONNECTION_PORT 12345
#define FNCODE 1

#define N_INVOKES 3

double get_timestamp() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (double)(tv.tv_sec * (uint64_t)1000000 + tv.tv_usec) / 1.0e6;
}

int main(int argc, char *argv[]) {
  ulog_set_level(LOG_ERROR);
  if (argc != 5) {
    fprintf(stderr,
            "Wrong number of arguments. use: "
            "./naaice_client local-ip remote-ip region-sizes\n"
            "Example: ./naaice_client 10.3.10.134 10.3.10.135 1024 logfile\n");
    return -1;
  };

  char *local_ip = argv[1];
  char *remote_ip = argv[2];

  size_t param_sizes[1];
  param_sizes[0] = atoi(argv[3]);

  char *params[1];
  params[0] = (char *)malloc(param_sizes[0] * sizeof(char));
  if (params[0] == NULL) {
    fprintf(stderr, "Failed to allocate memory for parameters.\n");
    return -1;
  }

  struct naaice_communication_context *comm_ctx = NULL;

  if (naaice_init_communication_context(&comm_ctx, 0, param_sizes, params, 1, 0,
                                        0, FNCODE, local_ip, remote_ip,
                                        CONNECTION_PORT)) {
    return -1;
  }
  if (naaice_setup_connection(comm_ctx)) {
    return -1;
  }

  if (naaice_set_input_mr(comm_ctx, 0)) {
    return -1;
  }
  if (naaice_set_output_mr(comm_ctx, 0)) {
    return -1;
  }

  uint8_t imm_bytes[4] = {0, 0, 0, 0};
  if (naaice_set_immediate(comm_ctx, imm_bytes)) {
    return -1;
  }

  double pre_setup = get_timestamp();
  if (naaice_register_mrs(comm_ctx)) {
    return -1;
  }
  if (naaice_do_mrsp(comm_ctx)) {
    return -1;
  }
  double start = get_timestamp();
  if (naaice_do_data_transfer(comm_ctx)) {
    return -1;
  }
  double end = get_timestamp();

  if (naaice_disconnect_and_cleanup(comm_ctx)) {
    return -1;
  }
  free(params[0]);

  FILE *logfile = fopen(argv[4], "a");
  if (logfile == NULL) {
    fprintf(stderr, "Failed to open logfile.\n");
    return -1;
  }
  fprintf(logfile, "%ld,%f,%f\n", param_sizes[0], start - pre_setup,
          end - start);
  fclose(logfile);

  return 0;
}
