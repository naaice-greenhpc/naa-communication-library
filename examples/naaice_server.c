/*
 * Basic example NAA server for loopback testing, used with naaice_client.c.
 * Change do_procedure() to alter the NAA's logic.
 */

/* Dependencies **************************************************************/

#include <naaice_swnaa.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/sysinfo.h>
#include <ulog.h>
#include <unistd.h>

#include "kernels/swnaa_kernels.h"

/* Master-worker model: the master accepts incoming connections, and each
 * worker thread holds one connection. User logic lives in
 * kernels/swnaa_kernel, selected by the function code.
 */

static volatile sig_atomic_t g_stop_requested = 0;

static void handle_shutdown_signal(__attribute__((unused)) int signo) { g_stop_requested = 1; }

static int install_signal_handlers(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = handle_shutdown_signal;

  if (sigaction(SIGINT, &sa, NULL) != 0) {
    return -1;
  }
  if (sigaction(SIGTERM, &sa, NULL) != 0) {
    return -1;
  }

  return 0;
}

static void cleanup_master_context(struct context *ctx) {
  if (ctx == NULL) {
    return;
  }

  if (ctx->master != NULL) {
    if (ctx->master->id != NULL) {
      rdma_destroy_id(ctx->master->id);
      ctx->master->id = NULL;
    }

    if (ctx->master->ev_channel != NULL) {
      rdma_destroy_event_channel(ctx->master->ev_channel);
      ctx->master->ev_channel = NULL;
    }
  }

  pthread_mutex_destroy(&ctx->lock);
  free(ctx->master);
  free(ctx->con_mng);
  free(ctx);
}

int main(int argc, __attribute__((unused)) char *argv[]) {
#ifndef ULOG_BUILD_DISABLED
  ulog_output_level_set_all(LOG_LEVEL);
#endif
  // Handle command line arguments.
  ulog_info("-- Handling Command Line Arguments --\n");
  if (argc > 1) {
    ulog_error("Server without any argument.\n");
    return -1;
  }

  if (install_signal_handlers()) {
    ulog_error("Failed to install signal handlers.\n");
    return -1;
  }

  struct context *ctx;

  if (naaice_swnaa_init_master(&ctx, SERVER_CONNECTION_PORT, rpc_function)) {
    ulog_error("Failed to initialize SWNAA master context.\n");
    return -1;
  }

  while (!g_stop_requested) {
    if (naaice_swnaa_poll_and_handle_connection_event(ctx)) {
      if (!g_stop_requested) {
        ulog_error("Failed to handle connection event.\n");
      }
    }
  }

  cleanup_master_context(ctx);

  return 0;
}