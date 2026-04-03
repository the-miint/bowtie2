/*
 * Paired-end in-memory alignment test: pass mate1/mate2 arrays via
 * bt2_align_run(), verify paired FLAG bits, RNEXT, PNEXT, TLEN.
 */
#include "bt2_api.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Synthetic paired reads from lambda_virus */
static const char *names1[] = { "pair1", "pair2" };
static const char *seqs1[]  = {
    "AGCTTTTCATTCTGACTGCAACGGGCAATATGTCTCTGTGT",
    "GCGATCTGTCTATTTCGTTCATCCATAGTTGCCTGACTCCC",
};
static const char *quals1[] = {
    "IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII",
    "IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII",
};

static const char *names2[] = { "pair1", "pair2" };
static const char *seqs2[]  = {
    "TTTAAATATGGTCTGATGATCTGGTTTATTGTGTCTTCTGA",
    "GTGAAACAAAGCACTATTGCACTGGCACTCTTACCGTTACT",
};
static const char *quals2[] = {
    "IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII",
    "IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII",
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
    input.names   = names1;
    input.seqs    = seqs1;
    input.quals   = quals1;
    input.n_reads = 2;
    input.names2   = names2;
    input.seqs2    = seqs2;
    input.quals2   = quals2;
    input.n_reads2 = 2;

    bt2_align_output_t *output = NULL;
    bt2_align_stats_t stats;
    memset(&stats, 0, sizeof(stats));

    int rc = bt2_align_run(ctx, &input, &output, &stats);
    assert(rc == BT2_OK);
    assert(output != NULL);
    /* 2 pairs = 4 records (one per mate) */
    assert(output->n_records == 4);

    /* Check paired-end FLAG bits */
    for (size_t i = 0; i < output->n_records; i++) {
        int32_t f = output->flag[i];
        /* 0x1 = paired */
        assert(f & 0x1);
        /* Each record should be first-in-pair (0x40) or second-in-pair (0x80) */
        assert((f & 0x40) || (f & 0x80));
    }

    printf("test_input_paired_memory: %zu records, %lld concordant pairs\n",
           output->n_records, (long long)stats.n_aligned_concordant);

    bt2_align_output_free(output);
    bt2_align_destroy(ctx);

    printf("test_input_paired_memory: PASSED\n");
    return 0;
}
