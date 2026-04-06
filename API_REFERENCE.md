# Bowtie2 C API Reference

Reentrant C API for embedding bowtie2 alignment and index building in
long-running processes. Designed for the
[GPL-boundary](https://github.com/the-miint/GPL-boundary) integration
pattern.

**Header:** `bt2_api.h`
**License:** GPL-3.0-or-later (same as bowtie2)
**Version:** 0.1.0

## Table of Contents

- [Quick Start](#quick-start)
- [Building](#building)
- [Aligner API](#aligner-api)
  - [Configuration](#aligner-configuration)
  - [Context Lifecycle](#aligner-context-lifecycle)
  - [File-Based Alignment](#file-based-alignment)
  - [In-Memory Alignment](#in-memory-alignment)
  - [Output Format](#output-format)
  - [Statistics](#alignment-statistics)
- [Index Builder API](#index-builder-api)
  - [Configuration](#builder-configuration)
  - [Context Lifecycle](#builder-context-lifecycle)
  - [Running a Build](#running-a-build)
- [Error Handling](#error-handling)
- [Log Callback](#log-callback)
- [Thread Safety](#thread-safety)
- [Memory Management](#memory-management)
- [Complete Examples](#complete-examples)
- [Linking](#linking)

---

## Quick Start

```c
#include "bt2_api.h"

/* Build an index */
bt2_build_config_t bconfig;
bt2_build_config_init(&bconfig);
const char *refs[] = {"genome.fa"};
bconfig.ref_paths = refs;
bconfig.n_ref_paths = 1;
bconfig.output_base = "my_index";

int err;
bt2_build_ctx_t *bctx = bt2_build_create(&bconfig, &err);
bt2_build_run(bctx, NULL);
bt2_build_destroy(bctx);

/* Align reads against it */
bt2_align_config_t aconfig;
bt2_align_config_init(&aconfig);
aconfig.index_path = "my_index";

bt2_align_ctx_t *actx = bt2_align_create(&aconfig, &err);

bt2_input_t input;
bt2_input_init(&input);
const char *names[] = {"read1", "read2"};
const char *seqs[]  = {"ACGTACGTACGT", "TGCATGCATGCA"};
input.names = names;
input.seqs = seqs;
input.n_reads = 2;

bt2_align_output_t *output;
bt2_align_stats_t stats;
bt2_align_run(actx, &input, &output, &stats);

for (size_t i = 0; i < output->n_records; i++) {
    printf("%s\t%d\t%s\t%lld\n",
           output->qname[i], output->flag[i],
           output->rname[i], (long long)output->pos[i]);
}

bt2_align_output_free(output);
bt2_align_destroy(actx);
```

---

## Building

### Prerequisites

- C++11 compiler (GCC 4.8+, Clang 3.3+)
- CMake 3.5+
- zlib development headers
- pthreads

### Library Targets

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

This produces:

| Target | Description |
|--------|-------------|
| `libbowtie2-align-s-lib.a` | Aligner library (32-bit index) |
| `libbowtie2-align-l-lib.a` | Aligner library (64-bit index) |
| `libbowtie2-build-s-lib.a` | Builder library (32-bit index) |
| `libbowtie2-build-l-lib.a` | Builder library (64-bit index) |
| `libbowtie2-combined-s-lib.a` | Both aligner + builder (32-bit) |

Use the `-s` (small) variants for references under 4 billion bases.
Use the `-l` (large) variants for larger references.
Use the combined library when you need both aligner and builder in one binary.

### Sanitizer Builds

```bash
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_ASAN=ON
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_TSAN=ON
```

ASan and TSan are mutually exclusive.

### Running Tests

```bash
cd build && ctest --output-on-failure
```

---

## Aligner API

### Aligner Configuration

```c
typedef struct {
    size_t       struct_size;    /* Set by bt2_align_config_init(). */
    const char  *index_path;    /* Path to index basename (required). */
    int64_t      seed;          /* Random seed. Default: 0. */
    int          nthreads;      /* Alignment threads. Default: 1. */
    int          preset;        /* BT2_PRESET_*. Default: BT2_PRESET_SENSITIVE. */
    int          local_align;   /* Nonzero for local alignment. Default: 0 (end-to-end). */
    int          quiet;         /* Suppress log output. Default: 1. */
    bt2_log_fn   log_fn;        /* Log callback. NULL to discard. */
    void        *log_user_data; /* Passed to log_fn. */
} bt2_align_config_t;
```

**`bt2_align_config_init(bt2_align_config_t *config)`**

Initialize config with safe defaults. Always call this before setting fields.
Safe to call with NULL (no-op).

#### Presets

| Constant | Value | Equivalent CLI Flag |
|----------|-------|-------------------|
| `BT2_PRESET_VERY_FAST` | 0 | `--very-fast` / `--very-fast-local` |
| `BT2_PRESET_FAST` | 1 | `--fast` / `--fast-local` |
| `BT2_PRESET_SENSITIVE` | 2 | `--sensitive` / `--sensitive-local` |
| `BT2_PRESET_VERY_SENSITIVE` | 3 | `--very-sensitive` / `--very-sensitive-local` |

When `local_align` is nonzero, the `-local` variant of each preset is used.

#### Field Details

| Field | Required | Default | Notes |
|-------|----------|---------|-------|
| `index_path` | Yes | NULL | Basename of a bowtie2 index (e.g. `"path/to/genome"` for `genome.1.bt2` etc.) |
| `seed` | No | 0 | Must be 0-2147483647. Controls random tie-breaking. |
| `nthreads` | No | 1 | Number of alignment worker threads. |
| `preset` | No | `BT2_PRESET_SENSITIVE` | Sensitivity/speed tradeoff. |
| `local_align` | No | 0 | 0 = end-to-end, nonzero = local alignment. |
| `quiet` | No | 1 | When 1 and `log_fn` is NULL, suppresses stderr/stdout output. |
| `log_fn` | No | NULL | Receives all diagnostic output. See [Log Callback](#log-callback). |
| `log_user_data` | No | NULL | Opaque pointer passed to `log_fn`. |

### Aligner Context Lifecycle

```c
/* Create */
bt2_align_ctx_t *bt2_align_create(
    const bt2_align_config_t *config,
    int *error_out);

/* Use (repeatable) */
int bt2_align_run(...);
int bt2_align_run_files(...);

/* Destroy */
void bt2_align_destroy(bt2_align_ctx_t *ctx);
```

**`bt2_align_create`** validates the config and checks that the index files
exist. Returns NULL on failure with the error code in `*error_out`.

**`bt2_align_destroy`** frees all resources. Safe to call with NULL.

A context can be reused for multiple `_run` / `_run_files` calls. It remains
valid after errors — you may retry with different input or destroy it.

### File-Based Alignment

```c
int bt2_align_run_files(
    bt2_align_ctx_t *ctx,
    const char **mate1_files, size_t n_mate1,
    const char **mate2_files, size_t n_mate2,
    bt2_align_output_t **output_out,
    bt2_align_stats_t *stats_out);
```

Aligns reads from FASTQ/FASTA files on disk.

- `mate1_files`: array of file paths for unpaired reads or mate 1.
- `mate2_files`: array of mate 2 file paths, or NULL for unpaired.
- `n_mate1` and `n_mate2` must be equal for paired-end.
- `output_out`: receives the output (caller must free with `bt2_align_output_free`).
- `stats_out`: receives statistics, or NULL to skip.

Returns `BT2_OK` on success.

### In-Memory Alignment

```c
int bt2_align_run(
    bt2_align_ctx_t *ctx,
    const bt2_input_t *input,
    bt2_align_output_t **output_out,
    bt2_align_stats_t *stats_out);
```

Aligns reads from in-memory string arrays. No file I/O.

#### Input Struct

```c
typedef struct {
    size_t        struct_size;  /* Set by bt2_input_init(). */
    const char  **names;        /* Read names [n_reads]. NULL defaults to "read". */
    const char  **seqs;         /* DNA sequences [n_reads]. Required. */
    const char  **quals;        /* Phred+33 quality strings [n_reads]. NULL for default (I). */
    size_t        n_reads;      /* Number of reads. 0 for empty input. */
    const char  **names2;       /* Mate 2 names. NULL for unpaired. */
    const char  **seqs2;        /* Mate 2 sequences. NULL for unpaired. */
    const char  **quals2;       /* Mate 2 qualities. NULL for default. */
    size_t        n_reads2;     /* Must equal n_reads for paired-end. */
} bt2_input_t;
```

Always call `bt2_input_init(&input)` before setting fields.

**Unpaired alignment:** Set `names`, `seqs`, `quals` (optional), and `n_reads`.
Leave all mate 2 fields NULL/zero.

**Paired-end alignment:** Set both mate 1 and mate 2 fields. `n_reads` must
equal `n_reads2`.

**Quality encoding:** Phred+33 ASCII (standard Illumina). When `quals` is NULL,
all bases receive quality 'I' (Phred 40).

**Empty input:** `n_reads = 0` returns `BT2_OK` with an empty output
(`output->n_records == 0`).

### Output Format

```c
typedef struct {
    size_t        struct_size;
    size_t        n_records;    /* Number of alignment records. */

    /* Mandatory SAM fields — arrays of length n_records */
    const char  **qname;        /* Read names. */
    int32_t      *flag;         /* SAM flags (bitfield). */
    const char  **rname;        /* Reference name. "*" if unmapped. */
    int64_t      *pos;          /* 1-based position. 0 if unmapped. */
    uint8_t      *mapq;         /* Mapping quality (0-255). */
    const char  **cigar;        /* CIGAR string. "*" if unmapped. */
    const char  **rnext;        /* Mate reference. "*" if unavailable. */
    int64_t      *pnext;        /* Mate position. 0 if unavailable. */
    int64_t      *tlen;         /* Template length. 0 if unavailable. */
    const char  **seq;          /* Read sequence. */
    const char  **qual;         /* Quality string. */

    /* Optional SAM tags — arrays of length n_records */
    int32_t      *tag_as;       /* AS:i alignment score. */
    int32_t      *tag_xs;       /* XS:i second-best score. INT32_MIN if absent. */
    int32_t      *tag_nm;       /* NM:i edit distance. */
    const char  **tag_md;       /* MD:Z mismatch string. */
    const char  **tag_yt;       /* YT:Z pairing classification. */

    void         *_backing;     /* Internal. Do not access. */
} bt2_align_output_t;
```

The output uses a **Structure of Arrays (SOA)** layout for Arrow/columnar
compatibility. All arrays have length `n_records`. String pointers reference
a single contiguous backing allocation — do not free individual strings.

**Free with `bt2_align_output_free(output)`**. Safe to call with NULL.

#### SAM Flag Bits

Common flag values (same as SAM specification):

| Bit | Hex | Meaning |
|-----|-----|---------|
| 0x1 | 1 | Paired |
| 0x2 | 2 | Proper pair |
| 0x4 | 4 | Unmapped |
| 0x8 | 8 | Mate unmapped |
| 0x10 | 16 | Reverse strand |
| 0x20 | 32 | Mate reverse strand |
| 0x40 | 64 | First in pair |
| 0x80 | 128 | Second in pair |
| 0x100 | 256 | Secondary alignment |

### Alignment Statistics

```c
typedef struct {
    int64_t  n_reads;               /* Total records (reads or mates). */
    int64_t  n_aligned;             /* Records with at least one alignment. */
    int64_t  n_unaligned;           /* Records with no alignment. */
    int64_t  n_aligned_concordant;  /* Concordant pairs (paired-end). */
    int64_t  elapsed_ms;            /* Wall-clock milliseconds. */
} bt2_align_stats_t;
```

Pass NULL for `stats_out` if you don't need statistics.

---

## Index Builder API

### Builder Configuration

```c
typedef struct {
    size_t       struct_size;    /* Set by bt2_build_config_init(). */
    const char **ref_paths;      /* Array of FASTA file paths (required). */
    size_t       n_ref_paths;    /* Number of reference files (required, > 0). */
    const char  *output_base;    /* Output index basename (required). */
    int          nthreads;       /* Threads. Default: 1. */
    int64_t      seed;           /* Random seed. Default: 0. */
    int          offrate;        /* SA sampling rate: 1 in 2^N. Default: 4. */
    int          packed;         /* Packed strings (less RAM, slower). Default: 0. */
    int          quiet;          /* Suppress verbose output. Default: 1. */
    bt2_log_fn   log_fn;         /* Log callback. NULL to discard. */
    void        *log_user_data;  /* Passed to log_fn. */
} bt2_build_config_t;
```

Always call `bt2_build_config_init(&config)` before setting fields.

#### Field Details

| Field | Required | Default | Notes |
|-------|----------|---------|-------|
| `ref_paths` | Yes | NULL | Array of FASTA file paths. Supports `.gz` and `.zst` compression. |
| `n_ref_paths` | Yes | 0 | Must be >= 1. |
| `output_base` | Yes | NULL | Index files written as `<base>.1.bt2`, `<base>.2.bt2`, etc. |
| `nthreads` | No | 1 | Threads for index construction. |
| `seed` | No | 0 | Must be 0-2147483647. |
| `offrate` | No | 4 | Suffix array sampling rate. Lower = larger index, faster alignment. |
| `packed` | No | 0 | Nonzero uses 2-bit packed encoding (half the RAM, slower build). |

### Builder Context Lifecycle

```c
/* Create — validates config and checks reference files exist */
bt2_build_ctx_t *bt2_build_create(
    const bt2_build_config_t *config,
    int *error_out);

/* Build — writes .bt2 files to disk */
int bt2_build_run(
    bt2_build_ctx_t *ctx,
    bt2_build_stats_t *stats_out);

/* Destroy */
void bt2_build_destroy(bt2_build_ctx_t *ctx);
```

### Running a Build

`bt2_build_run` writes 6 index files:

| File | Contents |
|------|----------|
| `<base>.1.bt2` | Forward BWT |
| `<base>.2.bt2` | Forward suffix array sampling |
| `<base>.3.bt2` | Reference metadata |
| `<base>.4.bt2` | Reference sequence (2-bit packed) |
| `<base>.rev.1.bt2` | Reverse BWT |
| `<base>.rev.2.bt2` | Reverse suffix array sampling |

On error, partially-written files are cleaned up automatically.

#### Builder Statistics

```c
typedef struct {
    int64_t  elapsed_ms;    /* Wall-clock milliseconds. */
} bt2_build_stats_t;
```

Pass NULL for `stats_out` if you don't need timing.

---

## Error Handling

### Error Codes

| Code | Value | Meaning |
|------|-------|---------|
| `BT2_OK` | 0 | Success |
| `BT2_ERR_NOMEM` | -1 | Out of memory |
| `BT2_ERR_INVALID_CONFIG` | -2 | Invalid or missing configuration |
| `BT2_ERR_INDEX` | -3 | Index file error (missing, corrupt, truncated) |
| `BT2_ERR_INPUT` | -4 | Input error (bad reads, missing files) |
| `BT2_ERR_INTERNAL` | -5 | Internal bowtie2 error |

### Error Reporting Functions

```c
/* Category name for an error code. Thread-safe. Never returns NULL. */
const char *bt2_strerror(int error_code);

/* Detailed message from last failed operation on this context.
   Returns "" if no error. Safe with NULL (returns ""). */
const char *bt2_align_last_error(const bt2_align_ctx_t *ctx);
const char *bt2_build_last_error(const bt2_build_ctx_t *ctx);
```

### Error Recovery

Contexts remain valid after errors. You can retry or destroy:

```c
int rc = bt2_align_run(ctx, &input, &output, NULL);
if (rc != BT2_OK) {
    fprintf(stderr, "Error: %s — %s\n",
            bt2_strerror(rc), bt2_align_last_error(ctx));
    /* Context is still valid — can retry with different input */
}
```

The library never calls `exit()` or `abort()`. All errors are returned as
error codes. A corrupt index returns `BT2_ERR_INDEX`, not a crash.

---

## Log Callback

```c
typedef void (*bt2_log_fn)(void *user_data, int level, const char *msg);
```

Set `config.log_fn` to receive diagnostic output. The callback receives
one line at a time (no trailing newline).

### Log Levels

| Level | Value | Content |
|-------|-------|---------|
| `BT2_LOG_ERROR` | 0 | Error messages |
| `BT2_LOG_WARN` | 1 | Warning messages |
| `BT2_LOG_INFO` | 2 | Progress, alignment summary, timing |
| `BT2_LOG_DEBUG` | 3 | Debug output (not currently used) |

Level detection is best-effort based on message prefix (`"Error:"`,
`"Warning:"`, etc.). Unrecognized prefixes default to `BT2_LOG_INFO`.

### Behavior

- **`log_fn` set, any `quiet` value:** Callback receives all output. The
  `quiet` flag is overridden internally so that progress messages and the
  alignment summary are produced for the callback.
- **`log_fn` NULL, `quiet` = 1:** All `std::cout` and `std::cerr` output
  is suppressed.
- **`log_fn` NULL, `quiet` = 0:** Output goes to stderr/stdout as normal
  (same as the native bowtie2 binary).

### Example

```c
void my_log(void *user_data, int level, const char *msg) {
    FILE *log = (FILE *)user_data;
    const char *prefix = level == BT2_LOG_ERROR ? "ERR" :
                         level == BT2_LOG_WARN  ? "WRN" : "INF";
    fprintf(log, "[%s] %s\n", prefix, msg);
}

bt2_align_config_t config;
bt2_align_config_init(&config);
config.log_fn = my_log;
config.log_user_data = stderr;
```

---

## Thread Safety

All API calls are serialized behind internal mutexes. You may call API
functions from any thread without external synchronization. However:

- Calls execute **sequentially**, not in parallel.
- Do not call API functions from within a `log_fn` callback (deadlock).
- Do not access a context from multiple threads simultaneously.

### What is safe

- Calling `bt2_align_run` from thread A and `bt2_build_run` from thread B.
- Calling `bt2_align_run` multiple times sequentially from the same thread.
- Calling `bt2_strerror` from any thread at any time.
- Calling `bt2_align_output_free` from any thread (after `_run` returns).

### What is NOT safe

- Calling `bt2_align_run` on the same context from two threads simultaneously.
- Calling `bt2_align_destroy` while another thread is in `bt2_align_run`.
- Calling any API function from within a `log_fn` callback.

---

## Memory Management

| Object | Allocated by | Freed by |
|--------|-------------|----------|
| `bt2_align_ctx_t` | `bt2_align_create` | `bt2_align_destroy` |
| `bt2_build_ctx_t` | `bt2_build_create` | `bt2_build_destroy` |
| `bt2_align_output_t` | `bt2_align_run` / `bt2_align_run_files` | `bt2_align_output_free` |
| Config structs | Caller (stack or heap) | Caller |
| `bt2_input_t` | Caller (stack or heap) | Caller |
| String arrays in input | Caller | Caller |
| String pointers in output | Library (backing buffer) | `bt2_align_output_free` |

**Config strings are deep-copied** by `_create`. You may free or modify
your config struct and its strings immediately after `_create` returns.

**Input arrays are read-only** during `_run`. You may free or modify them
after `_run` returns.

**Output string pointers** reference a single contiguous backing allocation.
Do not free individual strings — only call `bt2_align_output_free` on the
output struct itself.

All `_destroy` and `_free` functions are safe to call with NULL.

---

## Complete Examples

### Build Index and Align Reads

```c
#include "bt2_api.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    int err;

    /* Build index */
    bt2_build_config_t bconfig;
    bt2_build_config_init(&bconfig);
    const char *refs[] = {"reference.fa"};
    bconfig.ref_paths = refs;
    bconfig.n_ref_paths = 1;
    bconfig.output_base = "/tmp/my_index";
    bconfig.quiet = 1;

    bt2_build_ctx_t *bctx = bt2_build_create(&bconfig, &err);
    if (!bctx) {
        fprintf(stderr, "Build create failed: %s\n", bt2_strerror(err));
        return 1;
    }

    bt2_build_stats_t bstats;
    if (bt2_build_run(bctx, &bstats) != BT2_OK) {
        fprintf(stderr, "Build failed: %s\n", bt2_build_last_error(bctx));
        bt2_build_destroy(bctx);
        return 1;
    }
    printf("Index built in %lld ms\n", (long long)bstats.elapsed_ms);
    bt2_build_destroy(bctx);

    /* Align reads */
    bt2_align_config_t aconfig;
    bt2_align_config_init(&aconfig);
    aconfig.index_path = "/tmp/my_index";
    aconfig.preset = BT2_PRESET_VERY_SENSITIVE;
    aconfig.quiet = 1;

    bt2_align_ctx_t *actx = bt2_align_create(&aconfig, &err);
    if (!actx) {
        fprintf(stderr, "Align create failed: %s\n", bt2_strerror(err));
        return 1;
    }

    /* In-memory reads */
    bt2_input_t input;
    bt2_input_init(&input);
    const char *names[] = {"read_1", "read_2", "read_3"};
    const char *seqs[]  = {
        "ACGTACGTACGTACGTACGTACGTACGTACGT",
        "TGCATGCATGCATGCATGCATGCATGCATGCA",
        "NNNNNNNNNNNNNNNN"
    };
    input.names = names;
    input.seqs = seqs;
    input.n_reads = 3;
    /* quals = NULL: all bases get default quality 'I' (Phred 40) */

    bt2_align_output_t *output;
    bt2_align_stats_t astats;
    int rc = bt2_align_run(actx, &input, &output, &astats);
    if (rc != BT2_OK) {
        fprintf(stderr, "Align failed: %s\n", bt2_align_last_error(actx));
        bt2_align_destroy(actx);
        return 1;
    }

    /* Process results */
    printf("Aligned %lld/%lld reads in %lld ms\n",
           (long long)astats.n_aligned,
           (long long)astats.n_reads,
           (long long)astats.elapsed_ms);

    for (size_t i = 0; i < output->n_records; i++) {
        int mapped = !(output->flag[i] & 0x4);
        printf("  %s: %s at %s:%lld (MAPQ=%d, AS=%d)\n",
               output->qname[i],
               mapped ? "mapped" : "unmapped",
               output->rname[i],
               (long long)output->pos[i],
               output->mapq[i],
               output->tag_as[i]);
    }

    bt2_align_output_free(output);
    bt2_align_destroy(actx);
    return 0;
}
```

### Paired-End Alignment from Files

```c
bt2_align_config_t config;
bt2_align_config_init(&config);
config.index_path = "hg38_index";
config.nthreads = 8;
config.preset = BT2_PRESET_SENSITIVE;

int err;
bt2_align_ctx_t *ctx = bt2_align_create(&config, &err);

const char *m1[] = {"sample_R1.fastq.gz"};
const char *m2[] = {"sample_R2.fastq.gz"};
bt2_align_output_t *output;
bt2_align_stats_t stats;

int rc = bt2_align_run_files(ctx, m1, 1, m2, 1, &output, &stats);
if (rc == BT2_OK) {
    printf("%lld concordant pairs\n",
           (long long)stats.n_aligned_concordant);
}

bt2_align_output_free(output);
bt2_align_destroy(ctx);
```

### Batch Processing with Context Reuse

```c
bt2_align_ctx_t *ctx = bt2_align_create(&config, &err);

for (int batch = 0; batch < n_batches; batch++) {
    bt2_input_t input;
    bt2_input_init(&input);
    input.names = batch_names[batch];
    input.seqs  = batch_seqs[batch];
    input.quals = batch_quals[batch];
    input.n_reads = batch_sizes[batch];

    bt2_align_output_t *output;
    int rc = bt2_align_run(ctx, &input, &output, NULL);
    if (rc == BT2_OK) {
        process_batch(output);
        bt2_align_output_free(output);
    }
    /* Context remains valid for next batch, even after errors */
}

bt2_align_destroy(ctx);
```

---

## Linking

### CMake

```cmake
find_library(BT2_ALIGN_LIB bowtie2-align-s-lib PATHS /path/to/bowtie2/build)
find_library(BT2_BUILD_LIB bowtie2-build-s-lib PATHS /path/to/bowtie2/build)

target_link_libraries(my_app ${BT2_ALIGN_LIB})
# or for both:
find_library(BT2_COMBINED_LIB bowtie2-combined-s-lib PATHS /path/to/bowtie2/build)
target_link_libraries(my_app ${BT2_COMBINED_LIB})
```

### Manual

```bash
gcc my_app.c -I/path/to/bowtie2 \
    -L/path/to/bowtie2/build \
    -lbowtie2-align-s-lib \
    -lstdc++ -lz -lpthread -lm
```

The libraries are C++ static archives. C callers must link with `-lstdc++`.

### Rust (via GPL-boundary build.rs)

```rust
println!("cargo:rustc-link-search=/path/to/bowtie2/build");
println!("cargo:rustc-link-lib=static=bowtie2-align-s-lib");
println!("cargo:rustc-link-lib=stdc++");
println!("cargo:rustc-link-lib=z");
```
