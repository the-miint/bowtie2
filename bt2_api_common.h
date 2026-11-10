/*
 * Internal helpers shared by the aligner and builder libraries
 * (bt2_api_common.cpp). Not part of the public C API.
 */

#ifndef BT2_API_COMMON_H
#define BT2_API_COMMON_H

#include "bt2_api.h"

/*
 * Returns 1 if a config's nthreads can be used. Otherwise returns 0 and, if
 * log_fn is set, logs why at BT2_LOG_ERROR; bt2_align_create and
 * bt2_build_create then refuse the config with BT2_ERR_INVALID_CONFIG.
 * nthreads must be at least 1, and exactly 1 in a WebAssembly build without
 * pthreads, which cannot start a thread.
 */
int bt2_check_nthreads(int nthreads, bt2_log_fn log_fn, void *log_user_data);

#endif /* BT2_API_COMMON_H */
