/*
 * Internal header for bt2_api.cpp — not part of the public API.
 */

#ifndef BT2_API_INTERNAL_H
#define BT2_API_INTERNAL_H

#include "bt2_api.h"
#include <mutex>
#include <cstring>
#include <cstdio>

struct bt2_align_ctx {
	char *index_path_owned;     /* strdup'd copy of config.index_path */
	bt2_align_config_t config;  /* shallow copy; index_path points to index_path_owned */
	char last_error[2048];
};

/* Global mutex serializing all bowtie() calls.
   Thread-safety note: concurrent bt2_align_run_files calls on different
   contexts are safe (they serialize here) but will NOT run in parallel.
   Phase 4 will remove this once global state is encapsulated. */
extern std::mutex g_bowtie_mutex;

#endif /* BT2_API_INTERNAL_H */
