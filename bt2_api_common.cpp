/*
 * Shared API functions used by both aligner and builder libraries.
 * Included in both SEARCH_LIB_CPPS and BUILD_LIB_CPPS.
 */

#include "bt2_api.h"
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
