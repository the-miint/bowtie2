/*
 * Basic in-memory alignment test: load reads into arrays, align via
 * bt2_align_run(), verify output is non-empty and well-formed.
 */
#include "bt2_api.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Synthetic reads that align to lambda_virus */
static const char *names[] = {
    "synth_read_1",
    "synth_read_2",
    "synth_read_3",
};

static const char *seqs[] = {
    "AGCTTTTCATTCTGACTGCAACGGGCAATATGTCTCTGTGT",
    "TTTAAATATGGTCTGATGATCTGGTTTATTGTGTCTTCTGA",
    "GCGATCTGTCTATTTCGTTCATCCATAGTTGCCTGACTCCC",
};

static const char *quals[] = {
    "IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII", /* 41 I's */
    "IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII", /* 41 I's */
    "IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII", /* 41 I's */
};

int main(void) {
    bt2_align_config_t config;
    bt2_align_config_init(&config);
    config.index_path = "example/index/lambda_virus";
    config.quiet = 1;

    int err;
    bt2_align_ctx_t *ctx = bt2_align_create(&config, &err);
    assert(ctx != NULL);
    assert(err == BT2_OK);

    bt2_input_t input;
    bt2_input_init(&input);
    input.names = names;
    input.seqs = seqs;
    input.quals = quals;
    input.n_reads = 3;

    bt2_align_output_t *output = NULL;
    bt2_align_stats_t stats;
    memset(&stats, 0, sizeof(stats));

    int rc = bt2_align_run(ctx, &input, &output, &stats);
    assert(rc == BT2_OK);
    assert(output != NULL);
    assert(output->struct_size == sizeof(bt2_align_output_t));
    assert(output->n_records == 3);

    /* Verify SOA pointers */
    assert(output->qname != NULL);
    assert(output->flag != NULL);
    assert(output->rname != NULL);
    assert(output->pos != NULL);
    assert(output->mapq != NULL);
    assert(output->cigar != NULL);
    assert(output->seq != NULL);
    assert(output->qual != NULL);

    /* Verify read names match */
    for (size_t i = 0; i < 3; i++) {
        assert(output->qname[i] != NULL);
        assert(strcmp(output->qname[i], names[i]) == 0);
        assert(output->seq[i] != NULL);
        assert(strlen(output->seq[i]) > 0);
    }

    /* Stats */
    assert(stats.n_reads == 3);
    assert(stats.elapsed_ms >= 0);

    printf("test_input_memory: %zu records, %lld aligned\n",
           output->n_records, (long long)stats.n_aligned);

    bt2_align_output_free(output);
    bt2_align_destroy(ctx);

    printf("test_input_memory: PASSED\n");
    return 0;
}
