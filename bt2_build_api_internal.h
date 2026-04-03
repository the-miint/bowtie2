/*
 * Internal header for bt2_build_api.cpp — not part of the public API.
 */

#ifndef BT2_BUILD_API_INTERNAL_H
#define BT2_BUILD_API_INTERNAL_H

#include "bt2_api.h"
#include <cstring>

struct bt2_build_ctx {
	bt2_build_config_t config;
	char *output_base_owned;
	char **ref_paths_owned;    /* owned copies of reference paths */
	size_t n_ref_paths;
	char last_error[2048];
};

#endif /* BT2_BUILD_API_INTERNAL_H */
