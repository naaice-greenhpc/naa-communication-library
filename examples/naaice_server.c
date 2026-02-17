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
 * Florian Mikolajczak, florian.mikolajczak@uni-potsdam.de
 * Dylan Everingham, everingham@zib.de
 * Hannes Signer, signer@uni-potsdam.de
 * 04.02.2026
 *
 *****************************************************************************/

/* Dependencies **************************************************************/

#include "naaice.h"
#include <naaice_swnaa.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/sysinfo.h>
#include <ulog.h>
#include <unistd.h>

/* Constants *****************************************************************/

#define CONNECTION_PORT 12345

/** Idea: Master-Worker logic
 * master handles connection establishment for multiple connections
 * worker holds one connection per thread
 *
 * User-specified logic has to be implemented in the kernels/swnaa_kernel file
 * and are encoded with the function code.
 */

int main(int argc, __attribute__((unused)) char *argv[]) {
  // ulog_set_level(ulog_LEVEL);

  // Handle command line arguments.
  ulog_info("-- Handling Command Line Arguments --\n");
  if (argc != 1) {
    ulog_error("Server should be called without arguments.\n");
    return -1;
  }

  struct context *ctx;

  naaice_swnaa_init_master(&ctx, CONNECTION_PORT);
  while (1) {

    ctx->master->state = NAAICE_INIT;
    if (naaice_swnaa_setup_connection(ctx)) {
      ulog_error("Failed to setup connection.\n");
      continue; // don't exit, but listen for new connections
    }
  }

  return 0;
}