/*
 * Copyright 2026, bowtie2 contributors
 *
 * This file is part of Bowtie 2.
 *
 * Bowtie 2 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "bt2_api.h"
#include "bt2_build_api_internal.h"
#include "bt2_api_exception.h"
#include "bt2_log_streambuf.h"
#include <cinttypes>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>
#include <chrono>
#include <mutex>

extern "C" {
	int bowtie_build(int argc, const char **argv);
}

static std::mutex g_build_mutex;
/* bt2_strerror() is in bt2_api_common.cpp (shared with aligner library) */

/* ---- Helpers -------------------------------------------------------- */

static void set_build_last_error(bt2_build_ctx_t *ctx, const char *msg) {
	if (!ctx) return;
	strncpy(ctx->last_error, msg, sizeof(ctx->last_error) - 1);
	ctx->last_error[sizeof(ctx->last_error) - 1] = '\0';
}

/* ---- Config initialization ----------------------------------------- */

void bt2_build_config_init(bt2_build_config_t *config) {
	if (!config) return;
	memset(config, 0, sizeof(*config));
	config->struct_size    = sizeof(bt2_build_config_t);
	config->ref_paths      = NULL;
	config->n_ref_paths    = 0;
	config->output_base    = NULL;
	config->nthreads       = 1;
	config->seed           = 0;
	config->offrate        = 4;
	config->packed         = 0;
	config->quiet          = 1;
	config->log_fn         = NULL;
	config->log_user_data  = NULL;
}

/* ---- Lifecycle ------------------------------------------------------ */

bt2_build_ctx_t *bt2_build_create(const bt2_build_config_t *config,
                                  int *error_out) {
	if (!config || config->struct_size != sizeof(bt2_build_config_t)) {
		if (error_out) *error_out = BT2_ERR_INVALID_CONFIG;
		return NULL;
	}
	if (!config->output_base) {
		if (error_out) *error_out = BT2_ERR_INVALID_CONFIG;
		return NULL;
	}
	if (!config->ref_paths || config->n_ref_paths == 0) {
		if (error_out) *error_out = BT2_ERR_INPUT;
		return NULL;
	}

	/* Verify reference files exist */
	for (size_t i = 0; i < config->n_ref_paths; i++) {
		if (!config->ref_paths[i]) {
			if (error_out) *error_out = BT2_ERR_INPUT;
			return NULL;
		}
		FILE *f = fopen(config->ref_paths[i], "r");
		if (!f) {
			if (error_out) *error_out = BT2_ERR_INPUT;
			return NULL;
		}
		fclose(f);
	}

	bt2_build_ctx_t *ctx = (bt2_build_ctx_t *)calloc(1, sizeof(bt2_build_ctx_t));
	if (!ctx) {
		if (error_out) *error_out = BT2_ERR_NOMEM;
		return NULL;
	}

	ctx->output_base_owned = strdup(config->output_base);
	if (!ctx->output_base_owned) {
		free(ctx);
		if (error_out) *error_out = BT2_ERR_NOMEM;
		return NULL;
	}

	/* Deep-copy reference paths */
	ctx->ref_paths_owned = (char **)calloc(config->n_ref_paths, sizeof(char *));
	if (!ctx->ref_paths_owned) {
		free(ctx->output_base_owned);
		free(ctx);
		if (error_out) *error_out = BT2_ERR_NOMEM;
		return NULL;
	}
	ctx->n_ref_paths = config->n_ref_paths;
	for (size_t i = 0; i < config->n_ref_paths; i++) {
		ctx->ref_paths_owned[i] = strdup(config->ref_paths[i]);
		if (!ctx->ref_paths_owned[i]) {
			for (size_t j = 0; j < i; j++) free(ctx->ref_paths_owned[j]);
			free(ctx->ref_paths_owned);
			free(ctx->output_base_owned);
			free(ctx);
			if (error_out) *error_out = BT2_ERR_NOMEM;
			return NULL;
		}
	}

	ctx->config = *config;
	ctx->config.output_base = ctx->output_base_owned;
	ctx->config.ref_paths = (const char **)ctx->ref_paths_owned;
	ctx->last_error[0] = '\0';

	if (error_out) *error_out = BT2_OK;
	return ctx;
}

void bt2_build_destroy(bt2_build_ctx_t *ctx) {
	if (!ctx) return;
	for (size_t i = 0; i < ctx->n_ref_paths; i++) {
		free(ctx->ref_paths_owned[i]);
	}
	free(ctx->ref_paths_owned);
	free(ctx->output_base_owned);
	free(ctx);
}

/* ---- Error reporting ----------------------------------------------- */

const char *bt2_build_last_error(const bt2_build_ctx_t *ctx) {
	if (!ctx) return "";
	return ctx->last_error;
}

/* ---- Build --------------------------------------------------------- */

int bt2_build_run(bt2_build_ctx_t *ctx,
                  bt2_build_stats_t *stats_out) {
	if (!ctx) return BT2_ERR_INVALID_CONFIG;
	ctx->last_error[0] = '\0';

	/* Validate seed range — bowtie_build internally uses int */
	if (ctx->config.seed < 0 || ctx->config.seed > INT32_MAX) {
		set_build_last_error(ctx, "seed must be between 0 and 2147483647");
		return BT2_ERR_INVALID_CONFIG;
	}

	auto t_start = std::chrono::steady_clock::now();

	/* Build comma-separated reference path list */
	std::string ref_list;
	for (size_t i = 0; i < ctx->n_ref_paths; i++) {
		if (i > 0) ref_list += ",";
		ref_list += ctx->ref_paths_owned[i];
	}

	/* Build argv */
	std::vector<const char *> argv;
	argv.push_back("bowtie2-build");

	/* Threads */
	char threads_buf[32];
	snprintf(threads_buf, sizeof(threads_buf), "%d", ctx->config.nthreads);
	argv.push_back("--threads");
	argv.push_back(threads_buf);

	/* Seed */
	char seed_buf[32];
	snprintf(seed_buf, sizeof(seed_buf), "%" PRId64, (int64_t)ctx->config.seed);
	argv.push_back("--seed");
	argv.push_back(seed_buf);

	/* Offrate */
	char offrate_buf[32];
	snprintf(offrate_buf, sizeof(offrate_buf), "%d", ctx->config.offrate);
	argv.push_back("--offrate");
	argv.push_back(offrate_buf);

	/* Packed */
	if (ctx->config.packed) {
		argv.push_back("--packed");
	}

	/* Quiet */
	int effective_quiet = (ctx->config.log_fn != NULL) ? 0 : ctx->config.quiet;
	if (effective_quiet) {
		argv.push_back("--quiet");
	}

	/* Positional: reference, output_base */
	argv.push_back(ref_list.c_str());
	argv.push_back(ctx->output_base_owned);

	int rc;
	{
		std::lock_guard<std::mutex> lock(g_build_mutex);
		CerrRedirectGuard cerr_guard(ctx->config.log_fn,
		                             ctx->config.log_user_data,
		                             ctx->config.quiet,
		                             true /* redirect_cout: builder writes progress to cout */);

		rc = bowtie_build((int)argv.size(), argv.data());
	}

	if (rc != 0) {
		set_build_last_error(ctx, "bowtie2-build returned non-zero exit code");
		return BT2_ERR_INTERNAL;
	}

	if (stats_out) {
		memset(stats_out, 0, sizeof(*stats_out));
		auto t_end = std::chrono::steady_clock::now();
		auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
		stats_out->elapsed_ms = (int64_t)ms;
	}

	return BT2_OK;
}
