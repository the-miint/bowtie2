/*
 * Copyright 2026, bowtie2 contributors
 *
 * This file is part of Bowtie 2.
 *
 * Bowtie 2 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Bowtie 2 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Bowtie 2.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file bt2_api.cpp
 * @brief Implementation of the bowtie2 C library API.
 */

#include "bt2_api.h"
#include <cstring>

/* ---- Config initialization ----------------------------------------- */

void bt2_align_config_init(bt2_align_config_t *config) {
	if (!config) return;
	memset(config, 0, sizeof(*config));
	config->struct_size    = sizeof(bt2_align_config_t);
	config->index_path     = NULL;
	config->seed           = 0;
	config->nthreads       = 1;
	config->preset         = BT2_PRESET_SENSITIVE;
	config->local_align    = 0;
	config->quiet          = 1;
	config->log_fn         = NULL;
	config->log_user_data  = NULL;
}

/* ---- Error reporting ----------------------------------------------- */

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

const char *bt2_align_last_error(const bt2_align_ctx_t *ctx) {
	(void)ctx;
	return "";
}

/* ---- Lifecycle stubs (Phase 1 will implement these) ---------------- */

bt2_align_ctx_t *bt2_align_create(const bt2_align_config_t *config,
                                  int *error_out) {
	if (!config || config->struct_size != sizeof(bt2_align_config_t)) {
		if (error_out) *error_out = BT2_ERR_INVALID_CONFIG;
		return NULL;
	}
	if (!config->index_path) {
		if (error_out) *error_out = BT2_ERR_INVALID_CONFIG;
		return NULL;
	}
	/* Phase 1 will implement actual context creation */
	if (error_out) *error_out = BT2_ERR_INTERNAL;
	return NULL;
}

void bt2_align_destroy(bt2_align_ctx_t *ctx) {
	(void)ctx;
}

int bt2_align_run_files(bt2_align_ctx_t *ctx,
                        const char **mate1_files, size_t n_mate1,
                        const char **mate2_files, size_t n_mate2,
                        bt2_align_output_t **output_out,
                        bt2_align_stats_t *stats_out) {
	(void)ctx;
	(void)mate1_files;
	(void)n_mate1;
	(void)mate2_files;
	(void)n_mate2;
	(void)output_out;
	(void)stats_out;
	return BT2_ERR_INTERNAL;
}

void bt2_align_output_free(bt2_align_output_t *output) {
	(void)output;
}
