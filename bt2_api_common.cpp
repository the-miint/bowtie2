/*
 * Shared API functions used by both aligner and builder libraries.
 * Included in both SEARCH_LIB_CPPS and BUILD_LIB_CPPS.
 */

#include "bt2_api.h"
#include "bt2_api_common.h"
#include "bt2_log_streambuf.h"

/* Static mutex definition for CerrRedirectGuard */
std::mutex CerrRedirectGuard::g_cerr_redirect_mtx_;

const char *bt2_strerror(int error_code) {
	switch (error_code) {
		case BT2_OK:                 return "Success";
		case BT2_ERR_NOMEM:          return "Out of memory";
		case BT2_ERR_INVALID_CONFIG: return "Invalid configuration";
		case BT2_ERR_INDEX:          return "Index error";
		case BT2_ERR_INPUT:          return "Input error";
		case BT2_ERR_INTERNAL:       return "Internal error";
		default:                     return "Unknown error";
	}
}

int bt2_check_nthreads(int nthreads, bt2_log_fn log_fn, void *log_user_data) {
	const char *why = NULL;
	if (nthreads < 1) {
		why = "nthreads must be at least 1";
	}
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
	else if (nthreads > 1) {
		why = "nthreads must be 1: this WebAssembly build has no threads "
		      "(it was built without pthreads)";
	}
#endif
	if (why == NULL) return 1;
	if (log_fn) log_fn(log_user_data, BT2_LOG_ERROR, why);
	return 0;
}
