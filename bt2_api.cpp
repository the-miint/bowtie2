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
#include "bt2_api_exception.h"
#include "bt2_driver_api.h"
#include "bt2_log_streambuf.h"
#include "bt2_config_argv.h"
#include "aln_sink_columnar.h"
#include "pat.h"
#include "formats.h"
#include <cinttypes>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>
#include <chrono>
#include <mutex>

extern "C" {
	int bowtie(int argc, const char **argv);
}

std::mutex g_bowtie_mutex;

/* Globals for columnar sink injection into driver().
   When g_api_columnar_nthreads > 0, driver() creates an AlnSinkColumnar
   and stores it in g_api_sink. Caller retrieves results after driver().
   Protected by g_bowtie_mutex. Still needed because AlnSinkColumnar
   requires OutputQueue, which is stack-local inside driver(). */
int g_api_columnar_nthreads = 0;
AlnSink *g_api_sink = NULL;

/* ---- Helpers -------------------------------------------------------- */

static bool index_files_exist(const char *base) {
	/* Probe for .1.bt2 (small index) or .1.bt2l (large index).
	   Uses bt2_index_is_large() for the large probe to keep detection
	   logic in one place. */
	if (bt2_index_is_large(base)) return true;
	std::string probe_s = std::string(base) + ".1.bt2";
	FILE *f = fopen(probe_s.c_str(), "r");
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

	/* v0.2 fields — booleans/trim/k/strings already 0/NULL from memset */
	config->match_bonus       = -1;
	config->mismatch_penalty  = -1;
	config->n_penalty         = -1;
	config->read_gap_open     = -1;
	config->read_gap_extend   = -1;
	config->ref_gap_open      = -1;
	config->ref_gap_extend    = -1;
	config->min_insert        = -1;
	config->max_insert        = -1;
	config->seed_mismatches   = -1;
	config->seed_length       = -1;
	config->max_dp_failures   = -1;
	config->max_seed_rounds   = -1;
}

/* ---- Error reporting ----------------------------------------------- */
/* bt2_strerror() is in bt2_api_common.cpp (shared with builder library) */

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
	if (config->k > 0 && config->report_all) {
		if (error_out) *error_out = BT2_ERR_INVALID_CONFIG;
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

	/* Deep-copy optional strings */
	if (config->score_min) {
		ctx->score_min_owned = strdup(config->score_min);
		if (!ctx->score_min_owned) {
			free(ctx->index_path_owned);
			free(ctx);
			if (error_out) *error_out = BT2_ERR_NOMEM;
			return NULL;
		}
		ctx->config.score_min = ctx->score_min_owned;
	}
	if (config->rg_id) {
		ctx->rg_id_owned = strdup(config->rg_id);
		if (!ctx->rg_id_owned) {
			free(ctx->score_min_owned);
			free(ctx->index_path_owned);
			free(ctx);
			if (error_out) *error_out = BT2_ERR_NOMEM;
			return NULL;
		}
		ctx->config.rg_id = ctx->rg_id_owned;
	}

	ctx->last_error[0] = '\0';

	if (error_out) *error_out = BT2_OK;
	return ctx;
}

void bt2_align_destroy(bt2_align_ctx_t *ctx) {
	if (!ctx) return;
	free(ctx->index_path_owned);
	free(ctx->score_min_owned);
	free(ctx->rg_id_owned);
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

	/* Quiet — when log_fn is set, force quiet=0 so alignment summary
	   is produced and captured by the cerr redirect. */
	int effective_quiet = (ctx->config.log_fn != NULL) ? 0 : ctx->config.quiet;
	if (effective_quiet) {
		argv.push_back("--quiet");
	}

	/* Alignment options from config (shared with memory path) */
	ConfigArgvBufs option_bufs;
	append_config_argv(&ctx->config, argv, &option_bufs);

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

	/* Discard SAM text output — we capture results via AlnSinkColumnar.
	   bowtie() still needs a -S target; use platform null device. (#8) */
	argv.push_back("-S");
#ifdef _WIN32
	argv.push_back("NUL");
#else
	argv.push_back("/dev/null");
#endif

	int sink_nthreads = ctx->config.nthreads > 0 ? ctx->config.nthreads : 1;

	/* Lock, set up columnar sink globals, call bowtie, retrieve results */
	int rc;
	bt2_align_output_t *output = NULL;
	{
		std::lock_guard<std::mutex> lock(g_bowtie_mutex);
		CerrRedirectGuard cerr_guard(ctx->config.log_fn,
		                             ctx->config.log_user_data,
		                             ctx->config.quiet);
		g_api_columnar_nthreads = sink_nthreads;
		g_api_sink = NULL;

		rc = bowtie((int)argv.size(), argv.data());

		/* Retrieve the columnar sink that driver() created */
		AlnSinkColumnar *col_sink = static_cast<AlnSinkColumnar *>(g_api_sink);
		g_api_sink = NULL;
		g_api_columnar_nthreads = 0;

		if (rc != 0 || col_sink == NULL) {
			delete col_sink;
			set_last_error(ctx, "bowtie2 alignment returned non-zero exit code");
			return BT2_ERR_INTERNAL;
		}

		output = col_sink->finalize();
		delete col_sink;

		if (!output) {
			set_last_error(ctx, "Failed to finalize columnar output");
			return BT2_ERR_NOMEM;
		}
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

/* ---- Input initialization ------------------------------------------ */

void bt2_input_init(bt2_input_t *input) {
	if (!input) return;
	memset(input, 0, sizeof(*input));
	input->struct_size = sizeof(bt2_input_t);
}

/* ---- In-memory alignment ------------------------------------------- */

int bt2_align_run(bt2_align_ctx_t *ctx,
                  const bt2_input_t *input,
                  bt2_align_output_t **output_out,
                  bt2_align_stats_t *stats_out) {
	if (!ctx) return BT2_ERR_INVALID_CONFIG;
	if (!input || input->struct_size != sizeof(bt2_input_t)) {
		set_last_error(ctx, "Invalid input struct (wrong struct_size or NULL)");
		return BT2_ERR_INVALID_CONFIG;
	}
	if (!output_out) {
		set_last_error(ctx, "output_out must not be NULL");
		return BT2_ERR_INVALID_CONFIG;
	}

	/* Empty input: return empty output */
	if (input->n_reads == 0) {
		bt2_align_output_t *empty = (bt2_align_output_t *)calloc(1, sizeof(bt2_align_output_t));
		if (!empty) {
			set_last_error(ctx, "Out of memory allocating empty output");
			return BT2_ERR_NOMEM;
		}
		empty->struct_size = sizeof(bt2_align_output_t);
		empty->n_records = 0;
		empty->_backing = empty; /* match finalize() pattern: struct is its own backing */
		*output_out = empty;
		if (stats_out) memset(stats_out, 0, sizeof(*stats_out));
		return BT2_OK;
	}

	if (!input->seqs) {
		set_last_error(ctx, "input->seqs must not be NULL when n_reads > 0");
		return BT2_ERR_INPUT;
	}

	/* Paired-end detection: if either seqs2 or n_reads2 is set, both must be */
	bool has_seqs2 = (input->seqs2 != NULL);
	bool has_n2    = (input->n_reads2 > 0);
	if (has_seqs2 != has_n2) {
		set_last_error(ctx, "seqs2 and n_reads2 must both be set or both be zero/NULL");
		return BT2_ERR_INPUT;
	}
	bool paired = has_seqs2;
	if (paired && input->n_reads != input->n_reads2) {
		set_last_error(ctx, "n_reads and n_reads2 must be equal for paired-end");
		return BT2_ERR_INPUT;
	}

	ctx->last_error[0] = '\0';

	/* Validate seed range — bowtie internally uses int */
	if (ctx->config.seed < 0 || ctx->config.seed > INT32_MAX) {
		set_last_error(ctx, "seed must be between 0 and 2147483647");
		return BT2_ERR_INVALID_CONFIG;
	}

	auto t_start = std::chrono::steady_clock::now();

	int sink_nthreads = ctx->config.nthreads > 0 ? ctx->config.nthreads : 1;
	bt2_align_output_t *output = NULL;

	{
		std::lock_guard<std::mutex> lock(g_bowtie_mutex);

		/* Redirect cerr to log callback (or discard if quiet + no callback) */
		CerrRedirectGuard cerr_guard(ctx->config.log_fn,
		                             ctx->config.log_user_data,
		                             ctx->config.quiet);

		/* When log_fn is set, force quiet=0 so alignment summary is
		   produced (the CerrRedirectGuard captures it for the callback).
		   When log_fn is NULL, respect the quiet setting. */
		int effective_quiet = (ctx->config.log_fn != NULL) ? 0 : ctx->config.quiet;

		/* Set statics from API config */
		apply_config_to_statics(&ctx->config, effective_quiet);

		/* Build PatternParams for MemoryPatternSource */
		PatternParams pp(
			CMDLINE,       /* format: command-line sequences (tab-separated internally) */
			false,         /* interleaved */
			false,         /* fileParallel */
			(uint32_t)ctx->config.seed,
			1024,          /* max_buf (reads per batch) */
			false,         /* solexa64 */
			false,         /* phred64 */
			false,         /* intQuals */
			ctx->config.trim5,  /* trim5 */
			ctx->config.trim3,  /* trim3 */
			make_pair((short)0, (size_t)0), /* trimTo */
			0,             /* sampleLen */
			0,             /* sampleFreq */
			0,             /* skip */
			(uint64_t)-1,  /* upto (no limit) */
			sink_nthreads, /* nthreads */
			false,         /* fixName */
			false,         /* preserve_tags */
			false          /* align_paired_reads */
		);

		/* Create MemoryPatternSource(s) and PatternComposer */
		PatternComposer *patsrc = NULL;
		if (paired) {
			EList<PatternSource*> *srca = new EList<PatternSource*>();
			EList<PatternSource*> *srcb = new EList<PatternSource*>();
			srca->push_back(new MemoryPatternSource(
				input->names, input->seqs, input->quals,
				input->n_reads, pp));
			srcb->push_back(new MemoryPatternSource(
				input->names2, input->seqs2, input->quals2,
				input->n_reads2, pp));
			patsrc = new DualPatternComposer(srca, srcb, pp);
		} else {
			EList<PatternSource*> *src = new EList<PatternSource*>();
			src->push_back(new MemoryPatternSource(
				input->names, input->seqs, input->quals,
				input->n_reads, pp));
			patsrc = new SoloPatternComposer(src, pp);
		}

		/* Tell driver() to create a columnar sink (it needs the OutputQueue
		   which is stack-local inside driver). Phase 4 REFACTOR will
		   eventually move OutputQueue creation out of driver(). */
		g_api_columnar_nthreads = sink_nthreads;
		g_api_sink = NULL;

		const char *nulldev =
#ifdef _WIN32
			"NUL";
#else
			"/dev/null";
#endif
		try {
			if (bt2_index_is_large(ctx->config.index_path)) {
				driver_api_large(ctx->config.index_path, nulldev,
				                 patsrc, NULL);
			} else {
				driver_api_small(ctx->config.index_path, nulldev,
				                 patsrc, NULL);
			}
		} catch (const Bt2ApiException& e) {
			g_api_columnar_nthreads = 0;
			delete static_cast<AlnSinkColumnar *>(g_api_sink);
			g_api_sink = NULL;
			delete patsrc;
			set_last_error(ctx, e.what());
			return e.error_code;
		} catch (const std::bad_alloc&) {
			g_api_columnar_nthreads = 0;
			delete static_cast<AlnSinkColumnar *>(g_api_sink);
			g_api_sink = NULL;
			delete patsrc;
			set_last_error(ctx, "Out of memory during alignment");
			return BT2_ERR_NOMEM;
		} catch (...) {
			g_api_columnar_nthreads = 0;
			delete static_cast<AlnSinkColumnar *>(g_api_sink);
			g_api_sink = NULL;
			delete patsrc;
			set_last_error(ctx, "bowtie2 driver threw an exception");
			return BT2_ERR_INTERNAL;
		}

		/* Retrieve the columnar sink that driver() created */
		AlnSinkColumnar *col_sink = static_cast<AlnSinkColumnar *>(g_api_sink);
		g_api_sink = NULL;
		g_api_columnar_nthreads = 0;

		if (!col_sink) {
			delete patsrc;
			set_last_error(ctx, "No columnar sink after driver()");
			return BT2_ERR_INTERNAL;
		}

		output = col_sink->finalize();
		delete col_sink;
		delete patsrc;

		if (!output) {
			set_last_error(ctx, "Failed to finalize columnar output");
			return BT2_ERR_NOMEM;
		}
	}

	*output_out = output;

	if (stats_out) {
		memset(stats_out, 0, sizeof(*stats_out));
		int64_t n_records = (int64_t)output->n_records;
		int64_t aligned = 0;
		int64_t concordant = 0;
		for (size_t i = 0; i < output->n_records; i++) {
			int32_t f = output->flag[i];
			if (!(f & 0x4)) aligned++;
			if ((f & 0x2) && (f & 0x40)) concordant++;
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
