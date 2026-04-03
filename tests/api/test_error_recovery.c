/*
 * Phase 5a test: verify the library remains usable after an error.
 * Triggers an error (corrupt index), then creates a new context
 * with a valid index and verifies alignment succeeds.
 */
#include "bt2_api.h"
#include "test_helpers.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *names[] = { "recovery_read" };
static const char *seqs[]  = { "AGCTTTTCATTCTGACTGCAACGGGCAATATGTCTCTGTGT" };

int main(void) {
    const char *bad_index = "/tmp/bt2_test_bad_index_recovery";

    /* Step 1: Trigger an error with a corrupt index */
    create_truncated_index(bad_index);

    bt2_align_config_t config;
    bt2_align_config_init(&config);
    config.index_path = bad_index;
    config.quiet = 1;

    int err;
    bt2_align_ctx_t *ctx = bt2_align_create(&config, &err);
    assert(ctx != NULL);

    bt2_input_t input;
    bt2_input_init(&input);
    input.names = names;
    input.seqs = seqs;
    input.quals = NULL;
    input.n_reads = 1;

    bt2_align_output_t *output = NULL;
    int rc = bt2_align_run(ctx, &input, &output, NULL);
    printf("test_error_recovery: error phase returned %d\n", rc);
    assert(rc != BT2_OK);

    if (output) bt2_align_output_free(output);
    bt2_align_destroy(ctx);
    remove_truncated_index(bad_index);

    /* Step 2: Now use a valid index — library must still work */
    bt2_align_config_init(&config);
    config.index_path = "example/index/lambda_virus";
    config.quiet = 1;

    ctx = bt2_align_create(&config, &err);
    assert(ctx != NULL);
    assert(err == BT2_OK);

    output = NULL;
    bt2_align_stats_t stats;
    memset(&stats, 0, sizeof(stats));

    rc = bt2_align_run(ctx, &input, &output, &stats);
    assert(rc == BT2_OK);
    assert(output != NULL);
    assert(output->n_records == 1);
    assert(stats.n_reads == 1);

    printf("test_error_recovery: recovery phase returned %d, %zu records\n",
           rc, output->n_records);

    bt2_align_output_free(output);
    bt2_align_destroy(ctx);

    printf("test_error_recovery: PASSED\n");
    return 0;
}
