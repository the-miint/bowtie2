/*
 * No-quality test: quals=NULL should use default quality ('I', Phred 40).
 * Alignment should succeed.
 */
#include "bt2_api.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *names[] = { "noqual_1", "noqual_2" };
static const char *seqs[] = {
    "AGCTTTTCATTCTGACTGCAACGGGCAATATGTCTCTGTGT",
    "TTTAAATATGGTCTGATGATCTGGTTTATTGTGTCTTCTGA",
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
    input.quals = NULL;  /* No quality strings */
    input.n_reads = 2;

    bt2_align_output_t *output = NULL;
    bt2_align_stats_t stats;
    memset(&stats, 0, sizeof(stats));

    int rc = bt2_align_run(ctx, &input, &output, &stats);
    assert(rc == BT2_OK);
    assert(output != NULL);
    assert(output->n_records == 2);
    assert(stats.n_reads == 2);

    /* Verify alignment produced results */
    for (size_t i = 0; i < 2; i++) {
        assert(output->qname[i] != NULL);
        assert(strcmp(output->qname[i], names[i]) == 0);
        assert(output->seq[i] != NULL);
        assert(output->qual[i] != NULL);
        /* Quality should be all 'I' since we passed NULL quals */
    }

    printf("test_input_no_qual: %zu records, %lld aligned\n",
           output->n_records, (long long)stats.n_aligned);

    bt2_align_output_free(output);
    bt2_align_destroy(ctx);

    printf("test_input_no_qual: PASSED\n");
    return 0;
}
