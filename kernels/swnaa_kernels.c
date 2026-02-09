#include "swnaa_kernels.h"

/* Function Implementations **************************************************/
void custom_kernel_1(struct naaice_communication_context *comm_ctx) {

  for (unsigned int i = 0; i < comm_ctx->no_local_mrs; i++) {

    unsigned char *data = (unsigned char *)comm_ctx->mr_local_data[i].addr;

    for (unsigned int j = 0; j < comm_ctx->mr_local_data[i].size; j++) {
      data[j]++;
    }
  }
}

void custom_kernel_2(struct naaice_communication_context *comm_ctx) {

  for (unsigned int i = 0; i < comm_ctx->no_local_mrs; i++) {

    unsigned char *data = (unsigned char *)comm_ctx->mr_local_data[i].addr;

    for (unsigned int j = 0; j < comm_ctx->mr_local_data[i].size; j++) {
      data[j] += 2;
    }
  }
}

// match function codes to functions
int match_function_code(
    uint8_t fncode,
    void (**worker_func)(struct naaice_communication_context *)) {

  switch (fncode) {
  case 1:
    *worker_func = &custom_kernel_1;
    return 0;
  case 2:
    *worker_func = &custom_kernel_2;
    return 0;
  default:
    log_error("Received invalid function code %d.\n", fncode);
    return -1;
  }
}
