/*
 * Paired-end alignment test.
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

    const char *m1[] = {"example/reads/reads_1.fq"};
    const char *m2[] = {"example/reads/reads_2.fq"};
    bt2_align_output_t *output = NULL;
    bt2_align_stats_t stats;

    int rc = bt2_align_run_files(ctx, m1, 1, m2, 1, &output, &stats);
    assert(rc == BT2_OK);
    assert(output != NULL);
    assert(output->n_records > 0);

    /* Check that paired-end flags are present */
    int found_paired = 0;
    int found_mate1 = 0;
    int found_mate2 = 0;
    for (size_t i = 0; i < output->n_records; i++) {
        int32_t f = output->flag[i];
        if (f & 0x1) found_paired = 1;    /* paired */
        if (f & 0x40) found_mate1 = 1;    /* first in pair */
        if (f & 0x80) found_mate2 = 1;    /* second in pair */
    }
    assert(found_paired);
    assert(found_mate1);
    assert(found_mate2);

    /* Check that RNEXT/PNEXT/TLEN are populated for some records */
    int found_tlen_nonzero = 0;
    for (size_t i = 0; i < output->n_records; i++) {
        if (output->tlen[i] != 0) {
            found_tlen_nonzero = 1;
            break;
        }
    }
    assert(found_tlen_nonzero);

    printf("test_align_paired: %zu records, paired flags OK\n", output->n_records);

    bt2_align_output_free(output);
    bt2_align_destroy(ctx);

    printf("test_align_paired: PASSED\n");
    return 0;
}
