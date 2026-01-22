#include "ulog.h"
#include <naaice_ap2.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define CONNECTION_PORT 12345
#define FNCODE 1

#define N_INVOKES 1

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

  struct naa_handle *handle =
      (naa_handle *)calloc(1, sizeof(struct naa_handle));
  if (!handle) {
    log_error("Failed to create naa handle. Exiting.\n");
    return -1;
  }

  naa_status status;

  struct naa_param_t input_params[] = {
      {(void *)params[0], param_sizes[0], false}};

  struct naa_param_t output_params[] = {
      {(void *)params[0], param_sizes[0], false}};

  naa_create(FNCODE, input_params, 1, output_params, 1, handle);

  double start = get_timestamp();
  naa_invoke(handle);
  naa_wait(handle, &status);
  double end = get_timestamp();

  naa_finalize(handle);

  FILE *logfile = fopen(argv[4], "a");
  if (logfile == NULL) {
    fprintf(stderr, "Failed to open logfile.\n");
    return -1;
  }
  fprintf(logfile, "%ld,%f\n", param_sizes[0], end - start);
  fclose(logfile);

  return 0;
}