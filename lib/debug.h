/* Debug Macros **************************************************************/

#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>

#ifndef NDEBUG
#define DEBUG_ENABLED 1
#else
#define DEBUG_ENABLED 0
#endif

/**
 * Usage:
 * 	debug_print(fmt, ...);
 */
#define debug_print(...) \
            do { if (DEBUG_ENABLED) fprintf(stderr, __VA_ARGS__); } while (0)

#endif