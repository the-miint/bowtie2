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

#include "bt2_api.h"
#include "bt2_api_internal.h"
#include "bt2_sam_parse.h"
#include <cinttypes>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <string>
#include <vector>
#include <chrono>
#include <mutex>

extern "C" {
	int bowtie(int argc, const char **argv);
}

std::mutex g_bowtie_mutex;

/* ---- Helpers -------------------------------------------------------- */

static bool index_files_exist(const char *base) {
	/* Probe for .1.bt2 (small index) or .1.bt2l (large index) */
	std::string probe_s = std::string(base) + ".1.bt2";
	std::string probe_l = std::string(base) + ".1.bt2l";
	FILE *f = fopen(probe_s.c_str(), "r");
	if (f) { fclose(f); return true; }
	f = fopen(probe_l.c_str(), "r");
	if (f) { fclose(f); return true; }
	return false;
}

static void set_last_error(bt2_align_ctx_t *ctx, const char *msg) {
	if (!ctx) return;
	strncpy(ctx->last_error, msg, sizeof(ctx->last_error) - 1);
	ctx->last_error[sizeof(ctx->last_error) - 1] = '\0';
}

static const char *preset_flag(int preset, int local_align) {
	if (local_align) {
		switch (preset) {
			case BT2_PRESET_VERY_FAST:      return "--very-fast-local";
			case BT2_PRESET_FAST:           return "--fast-local";
			case BT2_PRESET_SENSITIVE:      return "--sensitive-local";
			case BT2_PRESET_VERY_SENSITIVE: return "--very-sensitive-local";
			default:                        return "--sensitive-local";
		}
	} else {
		switch (preset) {
			case BT2_PRESET_VERY_FAST:      return "--very-fast";
			case BT2_PRESET_FAST:           return "--fast";
			case BT2_PRESET_SENSITIVE:      return "--sensitive";
			case BT2_PRESET_VERY_SENSITIVE: return "--very-sensitive";
			default:                        return "--sensitive";
		}
	}
}

/* Read entire file contents into a malloc'd buffer. */
static char *read_file(const char *path, size_t *len_out) {
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (sz < 0) { fclose(f); return NULL; }
	char *buf = (char *)malloc((size_t)sz + 1);
	if (!buf) { fclose(f); return NULL; }
	size_t rd = fread(buf, 1, (size_t)sz, f);
	fclose(f);
	buf[rd] = '\0';
	if (len_out) *len_out = rd;
	return buf;
}

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
	if (!ctx) return "";
	return ctx->last_error;
}

/* ---- Lifecycle ------------------------------------------------------ */

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
	if (!index_files_exist(config->index_path)) {
		if (error_out) *error_out = BT2_ERR_INDEX;
		return NULL;
	}

	bt2_align_ctx_t *ctx = (bt2_align_ctx_t *)calloc(1, sizeof(bt2_align_ctx_t));
	if (!ctx) {
		if (error_out) *error_out = BT2_ERR_NOMEM;
		return NULL;
	}

	ctx->index_path_owned = strdup(config->index_path);
	if (!ctx->index_path_owned) {
		free(ctx);
		if (error_out) *error_out = BT2_ERR_NOMEM;
		return NULL;
	}

	ctx->config = *config;
	ctx->config.index_path = ctx->index_path_owned;
	ctx->last_error[0] = '\0';

	if (error_out) *error_out = BT2_OK;
	return ctx;
}

void bt2_align_destroy(bt2_align_ctx_t *ctx) {
	if (!ctx) return;
	free(ctx->index_path_owned);
	free(ctx);
}

int bt2_align_run_files(bt2_align_ctx_t *ctx,
                        const char **mate1_files, size_t n_mate1,
                        const char **mate2_files, size_t n_mate2,
                        bt2_align_output_t **output_out,
                        bt2_align_stats_t *stats_out) {
	if (!ctx) return BT2_ERR_INVALID_CONFIG;
	if (!mate1_files || n_mate1 == 0) {
		set_last_error(ctx, "No input files provided");
		return BT2_ERR_INPUT;
	}
	if (!output_out) {
		set_last_error(ctx, "output_out must not be NULL");
		return BT2_ERR_INVALID_CONFIG;
	}

	bool paired = (mate2_files != NULL && n_mate2 > 0);
	if (paired && n_mate1 != n_mate2) {
		set_last_error(ctx, "n_mate1 and n_mate2 must match for paired-end alignment");
		return BT2_ERR_INPUT;
	}

	ctx->last_error[0] = '\0';

	auto t_start = std::chrono::steady_clock::now();

	/* Build argv */
	std::vector<const char *> argv;
	argv.push_back("bowtie2");
	argv.push_back("-x");
	argv.push_back(ctx->config.index_path);

	/* Preset */
	argv.push_back(preset_flag(ctx->config.preset, ctx->config.local_align));

	/* Threads */
	char threads_buf[32];
	snprintf(threads_buf, sizeof(threads_buf), "%d", ctx->config.nthreads);
	argv.push_back("-p");
	argv.push_back(threads_buf);

	/* Seed */
	char seed_buf[32];
	snprintf(seed_buf, sizeof(seed_buf), "%" PRId64,
	         (int64_t)ctx->config.seed);
	argv.push_back("--seed");
	argv.push_back(seed_buf);

	/* Quiet */
	if (ctx->config.quiet) {
		argv.push_back("--quiet");
	}

	/* Input files */
	/* Build comma-separated file lists for bowtie2 */
	std::string mate1_list, mate2_list;
	for (size_t i = 0; i < n_mate1; i++) {
		if (i > 0) mate1_list += ",";
		mate1_list += mate1_files[i];
	}

	if (paired) {
		for (size_t i = 0; i < n_mate2; i++) {
			if (i > 0) mate2_list += ",";
			mate2_list += mate2_files[i];
		}
		argv.push_back("-1");
		argv.push_back(mate1_list.c_str());
		argv.push_back("-2");
		argv.push_back(mate2_list.c_str());
	} else {
		argv.push_back("-U");
		argv.push_back(mate1_list.c_str());
	}

	/* Output to tmpfile (respect TMPDIR for HPC environments) */
	const char *tmpdir = getenv("TMPDIR");
	if (!tmpdir || !*tmpdir) tmpdir = "/tmp";
	char tmppath[4096];
	snprintf(tmppath, sizeof(tmppath), "%s/bt2_api_XXXXXX", tmpdir);
	int tmpfd = mkstemp(tmppath);
	if (tmpfd < 0) {
		set_last_error(ctx, "Failed to create temporary file");
		return BT2_ERR_INTERNAL;
	}
	close(tmpfd);

	argv.push_back("-S");
	argv.push_back(tmppath);

	/* Lock and call bowtie */
	int rc;
	{
		std::lock_guard<std::mutex> lock(g_bowtie_mutex);
		rc = bowtie((int)argv.size(), argv.data());
	}

	if (rc != 0) {
		set_last_error(ctx, "bowtie2 alignment returned non-zero exit code");
		unlink(tmppath);
		return BT2_ERR_INTERNAL;
	}

	/* Read SAM output */
	size_t sam_len = 0;
	char *sam_text = read_file(tmppath, &sam_len);
	unlink(tmppath);

	if (!sam_text) {
		set_last_error(ctx, "Failed to read alignment output");
		return BT2_ERR_INTERNAL;
	}

	/* Parse SAM into output struct */
	bt2_align_output_t *output = NULL;
	rc = bt2_parse_sam(sam_text, sam_len, &output);
	free(sam_text);

	if (rc != BT2_OK) {
		set_last_error(ctx, "Failed to parse SAM output");
		return rc;
	}

	*output_out = output;

	/* Fill stats if requested.
	   For paired-end, each read pair produces 2 SAM records (one per mate).
	   n_reads counts input reads (pairs count as 2 reads, matching bowtie2's
	   stderr summary). We count reads by looking at first-in-pair flags. */
	if (stats_out) {
		memset(stats_out, 0, sizeof(*stats_out));
		int64_t n_records = (int64_t)output->n_records;
		int64_t aligned = 0;
		int64_t concordant = 0;
		for (size_t i = 0; i < output->n_records; i++) {
			int32_t f = output->flag[i];
			if (!(f & 0x4)) aligned++;
			if ((f & 0x2) && (f & 0x40)) concordant++; /* concordant, first mate */
		}
		stats_out->n_reads = n_records;
		stats_out->n_aligned = aligned;
		stats_out->n_unaligned = n_records - aligned;
		stats_out->n_aligned_concordant = concordant;

		auto t_end = std::chrono::steady_clock::now();
		auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
		stats_out->elapsed_ms = (int64_t)ms;
	}

	return BT2_OK;
}

void bt2_align_output_free(bt2_align_output_t *output) {
	if (!output) return;
	free(output->_backing);
}
