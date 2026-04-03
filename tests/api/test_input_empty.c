/*
 * Empty input test: n_reads=0 should return BT2_OK with n_records=0.
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

    bt2_input_t input;
    bt2_input_init(&input);
    /* n_reads = 0, all pointers NULL */

    bt2_align_output_t *output = NULL;
    bt2_align_stats_t stats;
    memset(&stats, 0, sizeof(stats));

    int rc = bt2_align_run(ctx, &input, &output, &stats);
    assert(rc == BT2_OK);
    assert(output != NULL);
    assert(output->n_records == 0);
    assert(stats.n_reads == 0);

    bt2_align_output_free(output);
    bt2_align_destroy(ctx);

    printf("test_input_empty: PASSED\n");
    return 0;
}
