#ifndef MALLOC_CLIENT
#define MALLOC_CLIENT

#include <stdint.h>
#include <stdlib.h>

#define MAX_FPGA_MNEMONIC_LEN 100
#define MAX_RMMS_ADDRESS_LEN 100
#define MAX_RMMS_MEM_TYPE_LEN 100
#define MAX_REQUEST_LEN 4096

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Allocates region_count regions of a given memory type on a specific FPGA on the server.
 * sizes holds region_count sizes to allocate; addresses is filled with the allocated addresses.
 * Returns the user id on success, -1 otherwise.
 */
int rmms_type_alloc(uint32_t *sizes, uint64_t *addresses, size_t region_count, const char *server,
                    int port, const char *fpga_mnemonic, const char *mem_type);

/*
 * Allocates region_count regions on a specific FPGA on the server.
 * sizes holds region_count sizes to allocate; addresses is filled with the allocated addresses.
 * Returns the user id on success, -1 otherwise.
 */
int rmms_alloc(uint32_t *sizes, uint64_t *addresses, size_t region_count, const char *server,
               int port, const char *fpga_mnemonic);

/*
 * Frees the memory region at free_addr that was registered under user_id.
 * Returns 0 on success, -1 otherwise.
 */
int rmms_free(int user_id, uint64_t free_addr, const char *server, int port,
              const char *fpga_mnemonic);

/*
 * Frees all memory registered under user_id.
 * Returns 0 on success, -1 otherwise.
 */
int rmms_free_all(int user_id, const char *server, int port, const char *fpga_mnemonic);

/*
 * Pings the server. Returns 0 on success.
 */
int rmms_say_hi(const char *server, int port);

#ifdef __cplusplus
}
#endif

#endif