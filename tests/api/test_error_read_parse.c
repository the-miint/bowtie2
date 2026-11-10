/*
 * An input error the read parser throws on returns an error from the API and
 * leaves the context usable, at one thread and at two. At one thread the
 * parser runs on the calling thread. At two, the reader thread parses the
 * first read of each batch and a worker thread the rest; an exception on
 * either is kept and rethrown on the calling thread once the workers stop.
 *
 * Two errors, one per entry point:
 *   - bt2_align_run_files, paired, with mate files of different read counts
 *     (tests/api/data/inline_reads.fq has two reads, one_read.fq one), which
 *     the reader finds;
 *   - bt2_align_run with a good read, then one whose quality string is
 *     shorter than its sequence, which the parser of the second read finds.
 * Each must return an error with a message. The test repeats both and checks
 * that the number of open file descriptors (/dev/fd; not counted in
 * WebAssembly, where it is the host's) does not grow: a failed run must close
 * the read files and the output file it opened. Then it aligns on the same
 * context.
 *
 * Explicit checks rather than assert(), so the test checks in any build.
 * Run from PROJECT_SOURCE_DIR, where the example index lives.
 */
#include "bt2_api.h"
#include <stdio.h>
#include <string.h>

#ifndef __EMSCRIPTEN__
#include <dirent.h>
#define COUNT_FDS 1
#endif

/* A WebAssembly build without pthreads aligns at one thread only. */
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
#define MAX_THREADS 1
#else
#define MAX_THREADS 2
#endif

#define INDEX   "example/index/lambda_virus"
#define MATES_1 "tests/api/data/inline_reads.fq"
#define MATES_2 "tests/api/data/one_read.fq"
#define N_RUNS  20

static int g_failures = 0;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: " __VA_ARGS__); \
        fprintf(stderr, "\n"); \
        g_failures++; \
    } \
} while (0)

/* Open file descriptors in this process; -1 if not counted. */
static int count_fds(void) {
#ifdef COUNT_FDS
    DIR *d = opendir("/dev/fd");
    if (d == NULL) return -1;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] != '.') n++;
    }
    closedir(d);
    return n;
#else
    return -1;
#endif
}

static void fail_files(bt2_align_ctx_t *ctx, int nthreads) {
    const char *m1[] = { MATES_1 };
    const char *m2[] = { MATES_2 };
    bt2_align_output_t *out = NULL;
    int rc = bt2_align_run_files(ctx, m1, 1, m2, 1, &out, NULL);
    CHECK(rc != BT2_OK, "[%d thread(s)] run_files with mismatched mates "
          "returned BT2_OK", nthreads);
    CHECK(out == NULL, "[%d thread(s)] run_files failed but set an output",
          nthreads);
    const char *msg = bt2_align_last_error(ctx);
    CHECK(msg != NULL && msg[0] != '\0', "[%d thread(s)] run_files failed "
          "with no message", nthreads);
    bt2_align_output_free(out);
}

static void fail_memory(bt2_align_ctx_t *ctx, int nthreads) {
    const char *names[] = { "lambda_1001", "short_qual" };
    const char *seqs[]  = { "GCAGCGCAACACCCTTATCTGGTTGCCGACGGATGGTGATG",
                            "GCAGCGCAACACCCTTATCTGG" };
    const char *quals[] = { "IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII", "III" };
    bt2_input_t input;
    bt2_input_init(&input);
    input.names = names;
    input.seqs = seqs;
    input.quals = quals;
    input.n_reads = 2;
    bt2_align_output_t *out = NULL;
    int rc = bt2_align_run(ctx, &input, &out, NULL);
    CHECK(rc != BT2_OK, "[%d thread(s)] run with a short quality string "
          "returned BT2_OK", nthreads);
    CHECK(out == NULL, "[%d thread(s)] run failed but set an output", nthreads);
    const char *msg = bt2_align_last_error(ctx);
    CHECK(msg != NULL && msg[0] != '\0', "[%d thread(s)] run failed with no "
          "message", nthreads);
    bt2_align_output_free(out);
}

static void check_threads(int nthreads) {
    bt2_align_config_t config;
    bt2_align_config_init(&config);
    config.index_path = INDEX;
    config.nthreads = nthreads;
    config.quiet = 1;

    int err = BT2_OK;
    bt2_align_ctx_t *ctx = bt2_align_create(&config, &err);
    if (ctx == NULL) {
        CHECK(0, "[%d thread(s)] bt2_align_create: %s", nthreads, bt2_strerror(err));
        return;
    }

    /* One failure of each first, so anything opened once per process is
       open before the count is taken. */
    fail_files(ctx, nthreads);
    fail_memory(ctx, nthreads);
    int fds_before = count_fds();
    for (int i = 0; i < N_RUNS; i++) {
        fail_files(ctx, nthreads);
        fail_memory(ctx, nthreads);
    }
    int fds_after = count_fds();
    CHECK(fds_after == fds_before,
          "[%d thread(s)] open file descriptors went from %d to %d over %d "
          "failed run pairs", nthreads, fds_before, fds_after, N_RUNS);

    /* The context still aligns. */
    const char *inputs[] = { MATES_1 };
    bt2_align_output_t *out = NULL;
    int rc = bt2_align_run_files(ctx, inputs, 1, NULL, 0, &out, NULL);
    CHECK(rc == BT2_OK, "[%d thread(s)] run_files after the failures: %s: %s",
          nthreads, bt2_strerror(rc), bt2_align_last_error(ctx));
    int aligned = 0;
    for (size_t i = 0; out != NULL && i < out->n_records; i++) {
        if (strcmp(out->qname[i], "lambda_1001") == 0 && !(out->flag[i] & 0x4)) {
            aligned = 1;
        }
    }
    CHECK(aligned, "[%d thread(s)] lambda_1001 did not align after the failures",
          nthreads);
    bt2_align_output_free(out);
    bt2_align_destroy(ctx);
    printf("test_error_read_parse[%d thread(s)]: open fds %d before, %d after "
           "(-1 if not counted)\n", nthreads, fds_before, fds_after);
}

int main(void) {
    for (int nthreads = 1; nthreads <= MAX_THREADS; nthreads++) {
        check_threads(nthreads);
    }
    if (g_failures != 0) {
        fprintf(stderr, "test_error_read_parse: %d check(s) failed\n", g_failures);
        return 1;
    }
    printf("test_error_read_parse: PASSED\n");
    return 0;
}
