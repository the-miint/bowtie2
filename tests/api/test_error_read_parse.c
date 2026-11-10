/*
 * An input error bowtie2 throws on while reading or parsing reads returns an
 * error from the API and leaves the context usable, at one thread and at
 * two. At one thread both run on the calling thread. At two, the reader
 * thread reads each batch and parses its first read, and a worker thread
 * parses the rest; an exception on either is kept and rethrown on the
 * calling thread once the workers stop.
 *
 * Two errors, one per entry point:
 *   - bt2_align_run_files, paired, with mate files of different read counts
 *     (tests/api/data/inline_reads.fq has two reads, one_read.fq one), which
 *     the reader finds;
 *   - bt2_align_run with a good read, then one whose quality string is
 *     shorter than its sequence, which parsing the second read finds.
 * Each must return BT2_ERR_INTERNAL with a message, and bowtie2's own
 * message, which names the problem, must reach log_fn: so the run failed on
 * the error under test and not on something else. The test repeats both and
 * checks that the number of open file descriptors does not grow (a failed
 * run must close the read files and the output file it opened); in
 * WebAssembly, where /dev/fd is the host's, it is not counted. Then it
 * aligns on the same context.
 *
 * Run from PROJECT_SOURCE_DIR, where the example index lives.
 */
#include "bt2_api.h"
#include "test_helpers.h"
#include <stdio.h>
#include <string.h>

#define INDEX   "example/index/lambda_virus"
#define MATES_1 "tests/api/data/inline_reads.fq"
#define MATES_2 "tests/api/data/one_read.fq"
#define N_RUNS  20

/* bowtie2's messages (pat.cpp) for the two errors. */
#define MATES_MSG "fewer reads in file specified with -2"
#define QUALS_MSG "Read short_qual has more read characters than quality values"

static int g_failures = 0;

/* Set when log_fn gets the message the current run should produce. log_fn
   is called one call at a time, and the message is looked for within a call:
   at two threads a line another thread writes at the same moment could share
   it, though none of these reads makes bowtie2 write anything else. */
static const char *g_want_msg;
static int g_saw_msg;

static void log_fn(void *user_data, int level, const char *msg) {
    (void)user_data;
    (void)level;
    if (g_want_msg != NULL && strstr(msg, g_want_msg) != NULL) g_saw_msg = 1;
}

static void expect_failure(const char *what, int nthreads, int rc,
                           bt2_align_ctx_t *ctx, bt2_align_output_t *out,
                           const char *want_msg) {
    CHECK_MSG(rc == BT2_ERR_INTERNAL, "[%d thread(s)] %s returned %s, not "
              "BT2_ERR_INTERNAL", nthreads, what, bt2_strerror(rc));
    CHECK_MSG(out == NULL, "[%d thread(s)] %s failed but set an output",
              nthreads, what);
    const char *msg = bt2_align_last_error(ctx);
    CHECK_MSG(msg != NULL && msg[0] != '\0', "[%d thread(s)] %s failed with no "
              "message", nthreads, what);
    CHECK_MSG(g_saw_msg, "[%d thread(s)] %s failed, but log_fn never got "
              "bowtie2's \"%s\"", nthreads, what, want_msg);
}

static void fail_files(bt2_align_ctx_t *ctx, int nthreads) {
    const char *m1[] = { MATES_1 };
    const char *m2[] = { MATES_2 };
    bt2_align_output_t *out = NULL;
    g_want_msg = MATES_MSG;
    g_saw_msg = 0;
    int rc = bt2_align_run_files(ctx, m1, 1, m2, 1, &out, NULL);
    expect_failure("run_files with mismatched mates", nthreads, rc, ctx, out,
                   MATES_MSG);
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
    g_want_msg = QUALS_MSG;
    g_saw_msg = 0;
    int rc = bt2_align_run(ctx, &input, &out, NULL);
    expect_failure("run with a short quality string", nthreads, rc, ctx, out,
                   QUALS_MSG);
    bt2_align_output_free(out);
}

static void check_threads(int nthreads) {
    bt2_align_config_t config;
    bt2_align_config_init(&config);
    config.index_path = INDEX;
    config.nthreads = nthreads;
    config.log_fn = log_fn;

    int err = BT2_OK;
    bt2_align_ctx_t *ctx = bt2_align_create(&config, &err);
    if (ctx == NULL) {
        CHECK_MSG(0, "[%d thread(s)] bt2_align_create: %s", nthreads,
                  bt2_strerror(err));
        return;
    }

    /* One failure of each first, so anything opened once per process is
       open before the count is taken. */
    fail_files(ctx, nthreads);
    fail_memory(ctx, nthreads);
    int fds_before = count_open_fds();
#ifndef __EMSCRIPTEN__
    CHECK_MSG(fds_before >= 0, "[%d thread(s)] open file descriptors could not "
              "be counted", nthreads);
#endif
    for (int i = 0; i < N_RUNS; i++) {
        fail_files(ctx, nthreads);
        fail_memory(ctx, nthreads);
    }
    int fds_after = count_open_fds();
    CHECK_MSG(fds_after == fds_before,
              "[%d thread(s)] open file descriptors went from %d to %d over %d "
              "failed run pairs", nthreads, fds_before, fds_after, N_RUNS);

    /* The context still aligns. */
    const char *inputs[] = { MATES_1 };
    bt2_align_output_t *out = NULL;
    g_want_msg = NULL;
    int rc = bt2_align_run_files(ctx, inputs, 1, NULL, 0, &out, NULL);
    CHECK_MSG(rc == BT2_OK, "[%d thread(s)] run_files after the failures: %s: %s",
              nthreads, bt2_strerror(rc), bt2_align_last_error(ctx));
    CHECK_MSG(aligned_by_name(out, "lambda_1001"), "[%d thread(s)] lambda_1001 "
              "did not align after the failures", nthreads);
    bt2_align_output_free(out);
    bt2_align_destroy(ctx);
    printf("test_error_read_parse[%d thread(s)]: open fds %d before, %d after "
           "(-1 if not counted)\n", nthreads, fds_before, fds_after);
}

int main(void) {
    int max_threads = TEST_HAS_THREADS ? 2 : 1;
    for (int nthreads = 1; nthreads <= max_threads; nthreads++) {
        check_threads(nthreads);
    }
    if (g_failures != 0) {
        fprintf(stderr, "test_error_read_parse: %d check(s) failed\n", g_failures);
        return 1;
    }
    printf("test_error_read_parse: PASSED\n");
    return 0;
}
