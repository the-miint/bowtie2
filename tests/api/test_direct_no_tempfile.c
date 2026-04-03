/*
 * Phase 4 test: verify bt2_align_run() works via direct driver path
 * (no temp files). Sets TMPDIR to a nonexistent directory — if the
 * implementation still writes temp files, mkstemp will fail and the
 * call will return an error. If the direct path is active, no temp
 * files are created and alignment succeeds.
 */
#include "bt2_api.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *names[] = { "direct_1", "direct_2", "direct_3" };
static const char *seqs[] = {
    "AGCTTTTCATTCTGACTGCAACGGGCAATATGTCTCTGTGT",
    "TTTAAATATGGTCTGATGATCTGGTTTATTGTGTCTTCTGA",
    "GCGATCTGTCTATTTCGTTCATCCATAGTTGCCTGACTCCC",
};

int main(void) {
    bt2_align_config_t config;
    bt2_align_config_init(&config);
    config.index_path = "example/index/lambda_virus";
    config.quiet = 1;

    int err;
    bt2_align_ctx_t *ctx = bt2_align_create(&config, &err);
    assert(ctx != NULL);

    bt2_input_t input;
    bt2_input_init(&input);
    input.names = names;
    input.seqs = seqs;
    input.quals = NULL; /* default quality */
    input.n_reads = 3;

    bt2_align_output_t *output = NULL;
    bt2_align_stats_t stats;
    memset(&stats, 0, sizeof(stats));

    /* Point TMPDIR at a nonexistent directory. If bt2_align_run()
       still tries to write temp files, it will fail. */
    setenv("TMPDIR", "/nonexistent_bt2_phase4_test_dir", 1);

    int rc = bt2_align_run(ctx, &input, &output, &stats);

    /* Restore TMPDIR */
    unsetenv("TMPDIR");

    assert(rc == BT2_OK);
    assert(output != NULL);
    assert(output->n_records == 3);
    assert(stats.n_reads == 3);

    /* Verify alignment results are valid */
    for (size_t i = 0; i < 3; i++) {
        assert(output->qname[i] != NULL);
        assert(strcmp(output->qname[i], names[i]) == 0);
        assert(output->seq[i] != NULL);
    }

    printf("test_direct_no_tempfile: %zu records, %lld aligned\n",
           output->n_records, (long long)stats.n_aligned);

    bt2_align_output_free(output);
    bt2_align_destroy(ctx);

    printf("test_direct_no_tempfile: PASSED\n");
    return 0;
}
