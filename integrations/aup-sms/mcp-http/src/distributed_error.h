#ifndef CBM_ASMS_DISTRIBUTED_ERROR_H
#define CBM_ASMS_DISTRIBUTED_ERROR_H
#include <stddef.h>

/* Add aup.exception 1.0 to an already authorized/redacted legacy error.
 * The input is borrowed. A non-NULL result is owned by the caller.
 * NULL leaves the complete legacy response unchanged (success or no room).
 * original is the error preceding a transport response-size failure, if any. */
char *cbm_asms_distributed_error(const char *legacy, const char *original, const char *correlation,
                                 int status, size_t limit);
#endif
