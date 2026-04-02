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
 * @brief Public C API for bowtie2 alignment library.
 *
 * This header provides a C API for bowtie2 following the GPL-boundary
 * integration pattern: config_init/create/run/destroy lifecycle with
 * structured error reporting.
 *
 * Thread safety:
 *   The current implementation serializes all alignment calls behind
 *   a global mutex. Concurrent calls on different bt2_align_ctx_t
 *   instances are safe but will execute sequentially. Do NOT assume
 *   independent parallel execution — this limitation will be removed
 *   in a future version once global state has been encapsulated into
 *   per-context structures (see Phase 4 of the API roadmap).
 *
 * All types use C-compatible representations (int for booleans,
 * explicit-width integers, void* callbacks) for FFI safety.
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
#define BT2_API_VERSION_MINOR 1
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
    int32_t      *tag_xs;     /**< XS:i second-best score. NULL if absent. */
    int32_t      *tag_nm;     /**< NM:i edit distance. NULL if absent. */
    const char  **tag_md;     /**< MD:Z mismatch string. NULL if absent. */
    const char  **tag_yt;     /**< YT:Z pairing type. NULL if absent. */

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
 * Free an output struct returned by bt2_align_run_files().
 * Safe to call with NULL (no-op).
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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* BT2_API_H */
