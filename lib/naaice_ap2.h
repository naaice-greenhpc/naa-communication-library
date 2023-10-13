/**************************************************************************//**
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
 * naaice_ap2.h
 *
 * Interface for NAAICE AP2 "MPI-Like" middleware layer.
 * 
 * Florian Mikolajczak, florian.mikolajczak@uni-potsdam.de
 * Dylan Everingham, everingham@zib.de
 * 
 * 12-10-2023
 * 
 *****************************************************************************/

#ifndef NAAICE_AP2_C_H
#define NAAICE_AP2_C_H

/**
 * TODO:
 * 
 * - Add support for configuration files:
 *      - File for NAA routine info
 *      - File for FPGA hardware info
 *      - File for connection info (addresses / ports)
 * 
 *  - Hash out RMS integration with these.
 * 
 *  - Implement timeouts.
 * 
 *  - Implement status struct. Probably just populate witth some relevant info
 *      in the communication context struct.
 * 
 */

/* Dependencies **************************************************************/

#include <stddef.h>
#include <stdbool.h>

/* Structs *******************************************************************/

// Struct to hold information about a single (input or output) parameter to
// an NAA routine.
typedef struct naa_param_t {
    void *addr;
    size_t size;
} naa_param_t;

// Struct to hold information about a NAA session.
typedef struct naa_handle {
    unsigned int function_code;
    struct naaice_communication_context *comm_ctx;
} naa_handle;

// Struct to hold status information about the NAA session.
typedef struct naa_status {} naa_status;

/* Public Functions **********************************************************/

int naa_create(unsigned int function_code, naa_param_t *params,
			   unsigned params_amount, naa_handle *handle);

int naa_invoke(naa_param_t *input_params, unsigned int input_amount,
			   naa_param_t output_param,
			   naa_handle *handle);

int naa_test(naa_handle *handle, bool *flag, naa_status *status);

int naa_wait(naa_handle *handle, naa_status *status);

int naa_finalize(naa_handle *handle);

#endif