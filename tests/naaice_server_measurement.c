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
 * naaice_server.c
 *
 * Application implementing a basic use case of the AP1 NAAICE communication
 * layer.
 * For use on a loopback test setup, in conjunction with naaice_client.c.
 *
 * The actual logic of the NAA procedure can be changed by putting whatever
 * you like in the implementation of do_procedure().
 *
 * Hannes Signer, signer@uni-potsdam.de
 * 04.02.2026
 *
 *****************************************************************************/

/* Dependencies **************************************************************/

#include "naaice.h"
#include <naaice_swnaa.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/sysinfo.h>
#include <ulog.h>
#include <unistd.h>

/* Constants *****************************************************************/

#define CONNECTION_PORT 12345

/** Idea: Master-Worker logic
master handles connection establishment for multiple connections
worker holds one connection per thread
*/

static volatile sig_atomic_t g_stop_requested = 0;

static void handle_shutdown_signal(__attribute__((unused)) int signo) {
  g_stop_requested = 1;
}

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
  if (argc != 1) {
    ulog_error("Server should be called without arguments.\n");
    return -1;
  }

  if (install_signal_handlers()) {
    ulog_error("Failed to install signal handlers.\n");
    return -1;
  }

  struct context *ctx;
  if (naaice_swnaa_init_master(&ctx, CONNECTION_PORT)) {
    ulog_error("Failed to initialize SWNAA master context.\n");
    return -1;
  }

  while (!g_stop_requested) {
    naaice_swnaa_poll_and_handle_connection_event(ctx);

    if (ctx->total_connections_lifetime > 0) {
      naaice_swnaa_poll_and_handle_connection_event(ctx);
      break;
    }
  }

  while (!g_stop_requested && ctx->con_mng->top < MAX_CONNECTIONS) {
    usleep(10);
  }

  ulog_info("All workers finished, shutting down.\n");

  cleanup_master_context(ctx);

  return 0;
}