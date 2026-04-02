/*
 * Basic alignment test: create context, align reads, verify output.
 */
#include "bt2_api.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    bt2_align_config_t config;
    bt2_align_config_init(&config);
    config.index_path = "example/index/lambda_virus";
    config.quiet = 1;

    int err;
    bt2_align_ctx_t *ctx = bt2_align_create(&config, &err);
    assert(ctx != NULL);
    assert(err == BT2_OK);

    const char *inputs[] = {"example/reads/longreads.fq"};
    bt2_align_output_t *output = NULL;
    bt2_align_stats_t stats;
    memset(&stats, 0, sizeof(stats));

    int rc = bt2_align_run_files(ctx, inputs, 1, NULL, 0, &output, &stats);
    assert(rc == BT2_OK);
    assert(output != NULL);
    assert(output->struct_size == sizeof(bt2_align_output_t));
    assert(output->n_records > 0);

    /* Verify SOA pointers are non-NULL */
    assert(output->qname != NULL);
    assert(output->flag != NULL);
    assert(output->rname != NULL);
    assert(output->pos != NULL);
    assert(output->mapq != NULL);
    assert(output->cigar != NULL);
    assert(output->rnext != NULL);
    assert(output->pnext != NULL);
    assert(output->tlen != NULL);
    assert(output->seq != NULL);
    assert(output->qual != NULL);

    /* Spot-check first record */
    assert(output->qname[0] != NULL);
    assert(strlen(output->qname[0]) > 0);
    assert(output->rname[0] != NULL);
    assert(output->seq[0] != NULL);
    assert(output->qual[0] != NULL);
    assert(output->cigar[0] != NULL);

    /* Stats should be populated */
    assert(stats.n_reads > 0);
    assert(stats.elapsed_ms >= 0);

    printf("test_align_basic: %zu records, %lld reads, %lld aligned\n",
           output->n_records,
           (long long)stats.n_reads,
           (long long)stats.n_aligned);

    bt2_align_output_free(output);
    bt2_align_destroy(ctx);

    printf("test_align_basic: PASSED\n");
    return 0;
}
