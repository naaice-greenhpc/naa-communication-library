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

int main(int argc, __attribute__((unused)) char *argv[]) {
  // ulog_set_level(LOG_LEVEL);

  // Handle command line arguments.
  ulog_info("-- Handling Command Line Arguments --\n");
  if (argc != 1) {
    ulog_error("Server should be called without arguments.\n");
    return -1;
  }

  struct context *ctx;
  if (naaice_swnaa_init_master(&ctx, CONNECTION_PORT)) {
    ulog_error("Failed to initialize SWNAA master context.\n");
    return -1;
  }

  while (true) {
    naaice_swnaa_poll_and_handle_connection_event(ctx);

    if (ctx->total_connections_lifetime > 0) {
      naaice_swnaa_poll_and_handle_connection_event(ctx);
      break;
    }
  }

  while (ctx->con_mng->top < MAX_CONNECTIONS) {
    usleep(10);
  }

  ulog_info("All workers finished, shutting down.\n");

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

  return 0;
}