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

static int run_once(bt2_align_ctx_t *ctx, int in_memory) {
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
    if (rc != BT2_OK) {
        fprintf(stderr, "FAIL: run: %s: %s\n", bt2_strerror(rc),
                bt2_align_last_error(ctx));
    }
    bt2_align_output_free(out);
    return rc;
}

/* Returns the number of failed checks. */
static int check(int memory_mapped, int in_memory) {
    const char *label = in_memory ? "memory" : "files";
    bt2_align_config_t config;
    bt2_align_config_init(&config);
    config.index_path = INDEX;
    config.quiet = 1;
    config.memory_mapped = memory_mapped;

    int err = BT2_OK;
    bt2_align_ctx_t *ctx = bt2_align_create(&config, &err);
    if (ctx == NULL) {
        fprintf(stderr, "FAIL: bt2_align_create: %s\n", bt2_strerror(err));
        return 1;
    }
    int failures = 0;
    if (run_once(ctx, in_memory) != BT2_OK) failures++;
    int before = count_open_fds();
    for (int i = 0; failures == 0 && i < N_RUNS; i++) {
        if (run_once(ctx, in_memory) != BT2_OK) failures++;
    }
    int after = count_open_fds();
    bt2_align_destroy(ctx);

    if (failures == 0 && after != before) {
        fprintf(stderr, "FAIL[%s, mm=%d]: open descriptors went from %d to %d "
                        "over %d runs\n", label, memory_mapped, before, after, N_RUNS);
        failures++;
    }
    if (failures == 0) {
        printf("test_fd_no_leak[%s, mm=%d]: %d open descriptors before and after "
               "%d runs\n", label, memory_mapped, before, N_RUNS);
    }
    return failures;
}

int main(void) {
    if (count_open_fds() < 0) {
        printf("test_fd_no_leak: SKIP (no /dev/fd of this process; in "
               "WebAssembly it is the host's)\n");
        return 0;
    }
    int failures = check(0, 0) + check(0, 1) + check(1, 0) + check(1, 1);
    if (failures != 0) {
        fprintf(stderr, "test_fd_no_leak: %d check(s) failed\n", failures);
        return 1;
    }
    printf("test_fd_no_leak: PASSED\n");
    return 0;
}
