/*
 * Repeated alignments on one context leave no file descriptor open. Each run
 * loads the reference from the index's .3/.4 files; a run that left one of
 * them open would leak a descriptor per run, which a long-lived process
 * running many batches turns into EMFILE.
 *
 * Both entry points, with and without --mm (memory_mapped), each run N_RUNS
 * times after one warm-up run; the count of open descriptors (/dev/fd) must
 * not change. Skipped in WebAssembly, where /dev/fd is the host's.
 *
 * Run from PROJECT_SOURCE_DIR, where the example index lives.
 */
#include "bt2_api.h"
#include "test_helpers.h"
#include <stdio.h>
#include <string.h>

#define INDEX  "example/index/lambda_virus"
#define READS  "tests/api/data/inline_reads.fq"
#define N_RUNS 10

static const char *NAMES[] = { "lambda_1001" };
static const char *SEQS[]  = { "GCAGCGCAACACCCTTATCTGGTTGCCGACGGATGGTGATG" };

static int g_failures = 0;

/* One run; whether it succeeded. */
static int run_once(bt2_align_ctx_t *ctx, int in_memory, const char *label,
                    int memory_mapped) {
    bt2_align_output_t *out = NULL;
    int rc;
    if (in_memory) {
        bt2_input_t input;
        bt2_input_init(&input);
        input.names = NAMES;
        input.seqs = SEQS;
        input.n_reads = 1;
        rc = bt2_align_run(ctx, &input, &out, NULL);
    } else {
        const char *inputs[] = { READS };
        rc = bt2_align_run_files(ctx, inputs, 1, NULL, 0, &out, NULL);
    }
    CHECK_MSG(rc == BT2_OK, "[%s, mm=%d] run: %s: %s", label, memory_mapped,
              bt2_strerror(rc), bt2_align_last_error(ctx));
    bt2_align_output_free(out);
    return rc == BT2_OK;
}

static void check(int memory_mapped, int in_memory) {
    const char *label = in_memory ? "memory" : "files";
    bt2_align_config_t config;
    bt2_align_config_init(&config);
    config.index_path = INDEX;
    config.quiet = 1;
    config.memory_mapped = memory_mapped;

    int err = BT2_OK;
    bt2_align_ctx_t *ctx = bt2_align_create(&config, &err);
    if (ctx == NULL) {
        CHECK_MSG(0, "[%s, mm=%d] bt2_align_create: %s", label, memory_mapped,
                  bt2_strerror(err));
        return;
    }
    int ok = run_once(ctx, in_memory, label, memory_mapped);
    int before = count_open_fds();
    for (int i = 0; ok && i < N_RUNS; i++) {
        ok = run_once(ctx, in_memory, label, memory_mapped);
    }
    int after = count_open_fds();
    bt2_align_destroy(ctx);
    if (!ok) return;

    CHECK_MSG(after == before, "[%s, mm=%d] open descriptors went from %d to %d "
              "over %d runs", label, memory_mapped, before, after, N_RUNS);
    printf("test_fd_no_leak[%s, mm=%d]: %d open descriptors before, %d after %d "
           "runs\n", label, memory_mapped, before, after, N_RUNS);
}

int main(void) {
    if (count_open_fds() < 0) {
        printf("test_fd_no_leak: SKIP (open descriptors not countable here; in "
               "WebAssembly /dev/fd is the host's)\n");
        return TEST_SKIP;
    }
    check(0, 0);
    check(0, 1);
    check(1, 0);
    check(1, 1);
    if (g_failures != 0) {
        fprintf(stderr, "test_fd_no_leak: %d check(s) failed\n", g_failures);
        return 1;
    }
    printf("test_fd_no_leak: PASSED\n");
    return 0;
}
