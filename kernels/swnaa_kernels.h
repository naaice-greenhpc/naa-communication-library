/**************************************************
 *
 * Example worker functions that can be executed on the software NAA side.
 *
 * These functions are matched to function codes received from the host and
 * executed by the worker threads on the NAA side. They operate on the memory
 * regions registered in the communication context, which are populated with
 * data from the host.
 *
 * For demonstration purposes, these functions perform simple operations (e.g.
 * incrementing each byte in the input memory regions). In a real application,
 * these would be replaced with more complex computations relevant to the use
 * case.
 *
 * Author: Hannes Signer, 2026
 **************************************************/

#include "naaice.h"
#include <stdint.h>
#include <ulog.h>

int match_function_code(
    uint8_t fncode,
    void (**worker_func)(struct naaice_communication_context *));

void custom_kernel_1(struct naaice_communication_context *comm_ctx);

void custom_kernel_2(struct naaice_communication_context *comm_ctx);