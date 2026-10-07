#include "swnaa_kernels.h"

#include <time.h>

/* Function Implementations **************************************************/
uint8_t custom_kernel_add(struct naaice_communication_context *comm_ctx) {
  if (comm_ctx->no_local_mrs != 3) {
    ulog_error("Expected 3 memory regions but got %d.", comm_ctx->no_local_mrs);
    return 1;
  }
  uint32_t *a = (uint32_t *)comm_ctx->mr_local_data[0].addr;
  uint32_t *b = (uint32_t *)comm_ctx->mr_local_data[1].addr;
  uint32_t *c = (uint32_t *)comm_ctx->mr_local_data[2].addr;
  for (unsigned int i = 0; i < comm_ctx->mr_local_data[0].size / sizeof(uint32_t); i++) {
    c[i] = a[i] + b[i];
  }
  return 0;
}

void custom_kernel_2(struct naaice_communication_context *comm_ctx) {
  for (unsigned int i = 0; i < comm_ctx->no_local_mrs; i++) {
    unsigned char *data = (unsigned char *)comm_ctx->mr_local_data[i].addr;

    for (unsigned int j = 0; j < comm_ctx->mr_local_data[i].size; j++) {
      data[j] += 2;
    }
  }
}

// Sleep for `immediate` seconds.
void delay_kernel(__attribute__((unused)) struct naaice_communication_context *comm_ctx) {
  ulog_debug("Delay for %d s.", comm_ctx->immediate);
  sleep(comm_ctx->immediate);
}

// Dispatch to the kernel selected by the function code.
uint8_t rpc_function(uint8_t fncode, struct naaice_communication_context *ctx) {
  uint8_t result = 0;
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);
  switch (fncode) {
    case 1:
      result = custom_kernel_add(ctx);
      break;
    case 2:
      custom_kernel_2(ctx);
      break;
    case 3:
      delay_kernel(ctx);
      break;
    default:
      ulog_error("Received invalid function code %d.\n", fncode);
      result = 1;
  }
  clock_gettime(CLOCK_MONOTONIC, &end);
  double ms = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
  ulog_info("RPC took %.3f ms\n", ms);
  return result;
}
