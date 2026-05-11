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
 * @file bt2_api.h
 * @brief Public C API for bowtie2 alignment and index building.
 *
 * This header provides a reentrant C API for bowtie2, designed for
 * embedding in long-running processes (e.g. GPL-boundary integration
 * with DuckDB/miint). Both the aligner and index builder follow the
 * same lifecycle: config_init -> create -> run -> destroy.
 *
 * == OVERVIEW ==
 *
 * Two independent APIs share this header:
 *   - Aligner: bt2_align_config_init/create/run/run_files/destroy
 *   - Builder: bt2_build_config_init/create/run/destroy
 *
 * Both return structured error codes (BT2_ERR_*) and never call
 * exit() or abort() from library code.
 *
 * == THREAD SAFETY ==
 *
 * All API calls are serialized behind internal mutexes. Concurrent
 * calls from multiple threads are safe but execute sequentially.
 * The aligner and builder use separate internal mutexes, but stream
 * redirection for the log callback adds an additional serialization
 * point — in practice, all API calls execute one at a time.
 *
 * DO NOT assume parallel execution — this is a sequential API.
 *
 * == SAFE USAGE PATTERNS ==
 *
 *   // Pattern 1: Build then align (same process)
 *   bt2_build_ctx_t *bctx = bt2_build_create(&bconfig, &err);
 *   bt2_build_run(bctx, &bstats);
 *   bt2_build_destroy(bctx);
 *
 *   bt2_align_ctx_t *actx = bt2_align_create(&aconfig, &err);
 *   bt2_align_run(actx, &input, &output, &astats);
 *   bt2_align_output_free(output);
 *   bt2_align_destroy(actx);
 *
 *   // Pattern 2: Multiple alignments, same context
 *   bt2_align_ctx_t *ctx = bt2_align_create(&config, &err);
 *   for (int i = 0; i < n_batches; i++) {
 *       bt2_align_run(ctx, &inputs[i], &outputs[i], NULL);
 *       process(outputs[i]);
 *       bt2_align_output_free(outputs[i]);
 *   }
 *   bt2_align_destroy(ctx);
 *
 *   // Pattern 3: Error recovery
 *   int rc = bt2_align_run(ctx, &bad_input, &output, NULL);
 *   if (rc != BT2_OK) {
 *       // Context remains valid — retry with different input
 *       rc = bt2_align_run(ctx, &good_input, &output, NULL);
 *   }
 *
 * == MEMORY MANAGEMENT ==
 *
 * - Contexts: caller creates with _create(), frees with _destroy().
 * - Align output: caller frees with bt2_align_output_free().
 * - Build output: index files written to disk, no output struct.
 * - Config structs: caller-owned, not copied (strings are deep-copied
 *   internally by _create). Safe to free/modify after _create returns.
 * - Input structs (bt2_input_t): caller-owned, read during _run only.
 *   Safe to free/modify after _run returns.
 *
 * == ERROR HANDLING ==
 *
 * All _run functions return BT2_OK (0) on success, negative BT2_ERR_*
 * on failure. Use bt2_strerror() for category name, _last_error() for
 * detailed message. Contexts remain valid after errors — callers may
 * retry or destroy.
 *
 * == LOG CALLBACK ==
 *
 * Set config.log_fn to receive diagnostic messages (errors, warnings,
 * alignment/build progress). The callback receives all output that
 * would otherwise go to stderr/stdout. When log_fn is set, quiet is
 * overridden internally so the callback receives the full output.
 * When log_fn is NULL and quiet is set, std::cout and std::cerr
 * output is suppressed via streambuf redirection.
 *
 * Log levels: BT2_LOG_ERROR, BT2_LOG_WARN, BT2_LOG_INFO, BT2_LOG_DEBUG.
 * Level detection is best-effort based on message prefix ("Error:",
 * "Warning:", etc.). Unrecognized prefixes default to BT2_LOG_INFO.
 *
 * == LIMITATIONS ==
 *
 * - Sequential execution only (mutex-serialized, not parallel).
 * - Seed parameter is clamped to int range (0 to 2147483647).
 * - struct_size field in config/output structs must not be set
 *   manually — always use the _init() functions.
 *
 * == ABI COMPATIBILITY ==
 *
 * All types use C-compatible representations (int for booleans,
 * explicit-width integers, void* callbacks) for FFI safety.
 * struct_size fields enable forward-compatible struct evolution:
 * new fields are appended; never reorder or remove existing fields.
 */

#ifndef BT2_API_H
#define BT2_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------
 * Version
 * -------------------------------------------------------------------- */

#define BT2_API_VERSION_MAJOR 0
#define BT2_API_VERSION_MINOR 3
#define BT2_API_VERSION_PATCH 0

/* --------------------------------------------------------------------
 * Error codes
 * -------------------------------------------------------------------- */

#define BT2_OK                    0
#define BT2_ERR_NOMEM            -1
#define BT2_ERR_INVALID_CONFIG   -2
#define BT2_ERR_INDEX            -3
#define BT2_ERR_INPUT            -4
#define BT2_ERR_INTERNAL         -5

/* --------------------------------------------------------------------
 * Log levels (for log_fn callback)
 * -------------------------------------------------------------------- */

#define BT2_LOG_ERROR   0
#define BT2_LOG_WARN    1
#define BT2_LOG_INFO    2
#define BT2_LOG_DEBUG   3

/* --------------------------------------------------------------------
 * Alignment presets
 * -------------------------------------------------------------------- */

#define BT2_PRESET_VERY_FAST       0
#define BT2_PRESET_FAST            1
#define BT2_PRESET_SENSITIVE       2
#define BT2_PRESET_VERY_SENSITIVE  3

/* --------------------------------------------------------------------
 * Mate orientation (for mate_orientation field)
 * -------------------------------------------------------------------- */

#define BT2_MATE_FR  0
#define BT2_MATE_RF  1
#define BT2_MATE_FF  2

/* --------------------------------------------------------------------
 * Opaque context type
 * -------------------------------------------------------------------- */

typedef struct bt2_align_ctx bt2_align_ctx_t;

/* --------------------------------------------------------------------
 * Log callback type
 * -------------------------------------------------------------------- */

typedef void (*bt2_log_fn)(void *user_data, int level, const char *msg);

/* --------------------------------------------------------------------
 * Configuration
 *
 * struct_size MUST be the first field. Call bt2_align_config_init()
 * to set struct_size and all defaults. New fields are appended at
 * the end; never reorder or remove existing fields.
 * -------------------------------------------------------------------- */

typedef struct {
    size_t       struct_size;    /**< Set by bt2_align_config_init(). DO NOT set manually. */
    const char  *index_path;    /**< Filesystem path to bowtie2 index basename (required). */
    int64_t      seed;          /**< Random seed for reproducibility. Default: 0. */
    int          nthreads;      /**< Number of alignment threads. Default: 1. */
    int          preset;        /**< Alignment preset (BT2_PRESET_*). Default: BT2_PRESET_SENSITIVE. */
    int          local_align;   /**< Nonzero for local alignment, 0 for end-to-end. Default: 0. */
    int          quiet;         /**< Nonzero to suppress non-error log messages. Default: 1.
                                     When log_fn is non-NULL, log_fn receives messages regardless
                                     of this flag. quiet only controls stderr fallback output when
                                     log_fn is NULL. */
    bt2_log_fn   log_fn;        /**< Log callback. NULL to discard log output. When non-NULL,
                                     receives all log messages regardless of quiet setting. */
    void        *log_user_data; /**< Passed to log_fn as user_data. */

    /* ---- v0.2 fields (appended for ABI compatibility) ---- */

    /* Reporting */
    int          k;              /**< Report up to k alignments per read (-k). 0 = not set (default 1). */
    int          report_all;     /**< Report all alignments (-a). Default: 0. */

    /* Trimming */
    int          trim5;          /**< Trim N bases from 5' end (--trim5). Default: 0. */
    int          trim3;          /**< Trim N bases from 3' end (--trim3). Default: 0. */

    /* Scoring — sentinel -1 means "use bowtie2 mode-dependent default" */
    int          match_bonus;       /**< Match bonus (--ma). Default: -1. */
    int          mismatch_penalty;  /**< Max mismatch penalty (--mp). Default: -1. */
    int          n_penalty;         /**< N penalty (--np). Default: -1. */
    int          read_gap_open;     /**< Read gap open penalty (--rdg arg1). Default: -1. */
    int          read_gap_extend;   /**< Read gap extend penalty (--rdg arg2). Default: -1. */
    int          ref_gap_open;      /**< Ref gap open penalty (--rfg arg1). Default: -1. */
    int          ref_gap_extend;    /**< Ref gap extend penalty (--rfg arg2). Default: -1. */
    const char  *score_min;         /**< Min score function string (--score-min). Default: NULL. */

    /* Paired-end */
    int          min_insert;        /**< Min fragment length (--minins/-I). Default: -1. */
    int          max_insert;        /**< Max fragment length (--maxins/-X). Default: -1. */
    int          mate_orientation;  /**< BT2_MATE_FR/RF/FF. Default: BT2_MATE_FR (0).
                                     Note: 0 means FR (active value), not "unset".
                                     Unlike -1 sentinel fields, there is no "unset"
                                     state — FR is always the default orientation. */
    int          no_mixed;          /**< Suppress unpaired for paired reads (--no-mixed). Default: 0. */
    int          no_discordant;     /**< Suppress discordant pairs (--no-discordant). Default: 0. */
    int          dovetail;          /**< Allow dovetail overlap (--dovetail). Default: 0. */
    int          no_contain;        /**< Disallow containment (--no-contain). Default: 0. */
    int          no_overlap;        /**< Disallow mate overlap (--no-overlap). Default: 0. */

    /* Strand */
    int          nofw;              /**< Don't align forward strand (--nofw). Default: 0. */
    int          norc;              /**< Don't align reverse complement (--norc). Default: 0. */

    /* Effort — sentinel -1 means "preset-dependent" */
    int          seed_mismatches;   /**< Max seed mismatches 0 or 1 (-N). Default: -1. */
    int          seed_length;       /**< Seed substring length 1-32 (-L). Default: -1. */
    int          max_dp_failures;   /**< Max consecutive extend failures (-D). Default: -1. */
    int          max_seed_rounds;   /**< Max seed rounds (-R). Default: -1. */

    /* SAM output */
    int          no_unal;           /**< Suppress unaligned reads (--no-unal). Default: 0. */
    int          xeq;               /**< Use =/X in CIGAR instead of M (--xeq). Default: 0. */
    const char  *rg_id;             /**< Read group ID (--rg-id). Default: NULL.
                                     Note: affects the aligner's internal RG handling
                                     but RG:Z tags are not yet included in
                                     bt2_align_output_t. */

    /* Other */
    int          ignore_quals;      /**< Treat all quals as 30 (--ignore-quals). Default: 0. */
    int          reorder;           /**< Preserve input order in output (--reorder). Default: 0. */
} bt2_align_config_t;

/* --------------------------------------------------------------------
 * Output — Structure of Arrays (SOA) layout
 *
 * Each array has length n_records. String pointers point into a
 * contiguous backing buffer (_backing). Free with
 * bt2_align_output_free().
 *
 * struct_size is set by the library when creating the output. It
 * allows detection of struct layout changes across versions.
 * -------------------------------------------------------------------- */

typedef struct {
    size_t        struct_size;/**< Set by the library. Callers can check against sizeof. */
    size_t        n_records;  /**< Number of alignment records. */

    /* Mandatory SAM fields */
    const char  **qname;      /**< Read names. */
    int32_t      *flag;       /**< SAM flags. */
    const char  **rname;      /**< Reference sequence names. "*" if unmapped. */
    int64_t      *pos;        /**< 1-based leftmost position. 0 if unmapped. */
    uint8_t      *mapq;       /**< Mapping quality. */
    const char  **cigar;      /**< CIGAR strings. "*" if unmapped. */
    const char  **rnext;      /**< Mate reference name. "*" if unavailable. */
    int64_t      *pnext;      /**< Mate position. 0 if unavailable. */
    int64_t      *tlen;       /**< Template length. 0 if unavailable. */
    const char  **seq;        /**< Read sequences. */
    const char  **qual;       /**< Quality strings. */

    /* Optional SAM tags — NULL if not computed */
    int32_t      *tag_as;     /**< AS:i alignment score. NULL if absent. */
    int32_t      *tag_xs;     /**< XS:i second-best score. INT32_MIN if absent. */
    int32_t      *tag_nm;     /**< NM:i edit distance. NULL if absent. */
    const char  **tag_md;     /**< MD:Z mismatch string. NULL if absent. */
    const char  **tag_yt;     /**< YT:Z pairing type. NULL if absent. */

    /* ---- v0.3 fields (appended for ABI compatibility) ---- */
    int32_t      *tag_ys;     /**< YS:i mate alignment score.
                                   INT32_MIN if not paired or no mate alignment. */
    int32_t      *tag_xn;     /**< XN:i ambiguous bases in covered ref. 0 if unaligned. */
    int32_t      *tag_xm;     /**< XM:i mismatches. 0 if unaligned. */
    int32_t      *tag_xo;     /**< XO:i gap opens. 0 if unaligned. */
    int32_t      *tag_xg;     /**< XG:i gap extensions (incl. opens). 0 if unaligned. */

    /* Internal — do not access directly */
    void         *_backing;   /**< Single allocation backing all arrays. */
} bt2_align_output_t;

/* --------------------------------------------------------------------
 * Statistics — value-only struct, no pointers, no free needed
 * -------------------------------------------------------------------- */

typedef struct {
    int64_t  n_reads;               /**< Total reads processed. */
    int64_t  n_aligned;             /**< Reads with at least one alignment. */
    int64_t  n_unaligned;           /**< Reads with no alignment. */
    int64_t  n_aligned_concordant;  /**< Concordant pairs (paired-end only). */
    int64_t  elapsed_ms;            /**< Wall-clock time in milliseconds. */
} bt2_align_stats_t;

/* --------------------------------------------------------------------
 * Input — In-memory reads for bt2_align_run()
 *
 * struct_size is set by bt2_input_init(). All string arrays have
 * length n_reads (or n_reads2 for mate 2). Quality strings use
 * Phred+33 ASCII encoding. Set quals to NULL for default quality
 * ('I', Phred 40) on all bases.
 *
 * For paired-end: set names2/seqs2/quals2 and n_reads2 (must equal
 * n_reads). For unpaired: leave mate 2 fields NULL / zero.
 * -------------------------------------------------------------------- */

typedef struct {
    size_t        struct_size;   /**< Set by bt2_input_init(). DO NOT set manually. */
    const char  **names;         /**< Read names, length n_reads. NULL array or
                                      NULL individual entries default to "read". */
    const char  **seqs;          /**< DNA sequences, length n_reads. */
    const char  **quals;         /**< Quality strings, length n_reads. NULL for default qual. */
    size_t        n_reads;       /**< Number of reads. */
    const char  **names2;        /**< Mate 2 names. NULL for unpaired. */
    const char  **seqs2;         /**< Mate 2 sequences. NULL for unpaired. */
    const char  **quals2;        /**< Mate 2 qualities. NULL for default qual. */
    size_t        n_reads2;      /**< Number of mate 2 reads. Must equal n_reads if non-zero. */
} bt2_input_t;

/* --------------------------------------------------------------------
 * Lifecycle functions
 * -------------------------------------------------------------------- */

/**
 * Initialize a config struct with default values.
 * Sets struct_size and all fields to safe defaults.
 * Safe to call with NULL (no-op).
 * After calling this, set at minimum config->index_path before
 * calling bt2_align_create().
 */
void bt2_align_config_init(bt2_align_config_t *config);

/**
 * Create an alignment context from a config.
 * Returns NULL on failure. If error_out is non-NULL, it receives
 * the error code (BT2_ERR_NOMEM, BT2_ERR_INVALID_CONFIG, etc.).
 * Use bt2_strerror(*error_out) for a human-readable category.
 */
bt2_align_ctx_t *bt2_align_create(const bt2_align_config_t *config,
                                  int *error_out);

/**
 * Destroy an alignment context and free all associated resources.
 * Safe to call with NULL (no-op).
 */
void bt2_align_destroy(bt2_align_ctx_t *ctx);

/**
 * Run alignment on FASTQ/FASTA files.
 *
 * @param ctx         Alignment context.
 * @param mate1_files Array of mate1 (or unpaired) file paths.
 * @param n_mate1     Number of mate1 files.
 * @param mate2_files Array of mate2 file paths (NULL for unpaired).
 * @param n_mate2     Number of mate2 files (0 for unpaired).
 * @param output_out  On success, receives a pointer to the output.
 *                    Caller must free with bt2_align_output_free().
 * @param stats_out   On success, receives alignment statistics.
 *                    May be NULL if stats are not needed.
 * @return BT2_OK on success, negative error code on failure.
 */
int bt2_align_run_files(bt2_align_ctx_t *ctx,
                        const char **mate1_files, size_t n_mate1,
                        const char **mate2_files, size_t n_mate2,
                        bt2_align_output_t **output_out,
                        bt2_align_stats_t *stats_out);

/**
 * Initialize an input struct with default values.
 * Sets struct_size and zero-fills all fields.
 * Safe to call with NULL (no-op).
 */
void bt2_input_init(bt2_input_t *input);

/**
 * Run alignment on in-memory reads.
 *
 * @param ctx         Alignment context.
 * @param input       In-memory reads (names, sequences, qualities).
 * @param output_out  On success, receives a pointer to the output.
 *                    Caller must free with bt2_align_output_free().
 * @param stats_out   On success, receives alignment statistics.
 *                    May be NULL if stats are not needed.
 * @return BT2_OK on success, negative error code on failure.
 */
int bt2_align_run(bt2_align_ctx_t *ctx,
                  const bt2_input_t *input,
                  bt2_align_output_t **output_out,
                  bt2_align_stats_t *stats_out);

/**
 * Free an output struct returned by bt2_align_run_files() or
 * bt2_align_run(). Safe to call with NULL (no-op).
 */
void bt2_align_output_free(bt2_align_output_t *output);

/* --------------------------------------------------------------------
 * Error reporting
 * -------------------------------------------------------------------- */

/**
 * Return a static string describing an error code category.
 * Safe to call from any thread. Never returns NULL.
 */
const char *bt2_strerror(int error_code);

/**
 * Return a detailed error message from the last failed operation
 * on this context. Valid until the next API call on the same context
 * or until bt2_align_destroy().
 * Returns "" if no error has occurred. Safe to call with NULL (returns "").
 */
const char *bt2_align_last_error(const bt2_align_ctx_t *ctx);

/* ====================================================================
 * Index Builder API
 * ==================================================================== */

/* --------------------------------------------------------------------
 * Opaque builder context
 * -------------------------------------------------------------------- */

typedef struct bt2_build_ctx bt2_build_ctx_t;

/* --------------------------------------------------------------------
 * Builder configuration
 *
 * struct_size MUST be first field. Call bt2_build_config_init() to
 * set struct_size and all defaults. New fields appended at end only.
 * -------------------------------------------------------------------- */

typedef struct {
    size_t       struct_size;    /**< Set by bt2_build_config_init(). DO NOT set manually. */
    const char **ref_paths;      /**< Array of reference FASTA file paths. */
    size_t       n_ref_paths;    /**< Number of reference file paths. */
    const char  *output_base;    /**< Output index basename (required). */
    int          nthreads;       /**< Number of threads. Default: 1. */
    int64_t      seed;           /**< Random seed. Default: 0. */
    int          offrate;        /**< SA sampling: 1 in 2^N. Default: 4. */
    int          packed;         /**< Nonzero for packed strings (less RAM). Default: 0. */
    int          quiet;          /**< Nonzero to suppress verbose output. Default: 1. */
    bt2_log_fn   log_fn;         /**< Log callback. NULL to discard. */
    void        *log_user_data;  /**< Passed to log_fn. */
} bt2_build_config_t;

/* --------------------------------------------------------------------
 * Builder statistics — value-only struct, no free needed
 * -------------------------------------------------------------------- */

typedef struct {
    int64_t  elapsed_ms;    /**< Wall-clock time in milliseconds. */
} bt2_build_stats_t;

/* --------------------------------------------------------------------
 * Builder lifecycle
 * -------------------------------------------------------------------- */

void bt2_build_config_init(bt2_build_config_t *config);

bt2_build_ctx_t *bt2_build_create(const bt2_build_config_t *config,
                                  int *error_out);

/**
 * Build a bowtie2 index. Writes .bt2 files at config->output_base.
 * @return BT2_OK on success, negative error code on failure.
 */
int bt2_build_run(bt2_build_ctx_t *ctx,
                  bt2_build_stats_t *stats_out);

void bt2_build_destroy(bt2_build_ctx_t *ctx);

const char *bt2_build_last_error(const bt2_build_ctx_t *ctx);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* --------------------------------------------------------------------
 * ABI regression canaries
 *
 * Pin the ordering of v0.3 optional-tag fields in bt2_align_output_t. If a
 * future change reorders or removes any of these, compilation breaks here
 * before any binary callers can drift. This is a structural check (relative
 * offsets), not a value check, so it tolerates compiler-specific padding.
 * -------------------------------------------------------------------- */

#if defined(__cplusplus)
#  define BT2_API_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#  define BT2_API_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#else
#  define BT2_API_STATIC_ASSERT(cond, msg) /* pre-C11: skip */
#endif

BT2_API_STATIC_ASSERT(
    offsetof(bt2_align_output_t, tag_ys) > offsetof(bt2_align_output_t, tag_yt),
    "ABI: bt2_align_output_t.tag_ys must be appended after tag_yt");
BT2_API_STATIC_ASSERT(
    offsetof(bt2_align_output_t, tag_xn) > offsetof(bt2_align_output_t, tag_ys),
    "ABI: bt2_align_output_t.tag_xn must follow tag_ys");
BT2_API_STATIC_ASSERT(
    offsetof(bt2_align_output_t, tag_xm) > offsetof(bt2_align_output_t, tag_xn),
    "ABI: bt2_align_output_t.tag_xm must follow tag_xn");
BT2_API_STATIC_ASSERT(
    offsetof(bt2_align_output_t, tag_xo) > offsetof(bt2_align_output_t, tag_xm),
    "ABI: bt2_align_output_t.tag_xo must follow tag_xm");
BT2_API_STATIC_ASSERT(
    offsetof(bt2_align_output_t, tag_xg) > offsetof(bt2_align_output_t, tag_xo),
    "ABI: bt2_align_output_t.tag_xg must follow tag_xo");

#endif /* BT2_API_H */
