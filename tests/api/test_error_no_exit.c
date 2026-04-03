/*
 * Phase 5a test: verify that a corrupted/truncated index file returns
 * BT2_ERR_INDEX (or another error code) instead of calling exit()
 * and killing the process.
 *
 * Creates a minimal truncated .bt2 file, attempts alignment, verifies
 * the library returns an error rather than terminating.
 */
#include "bt2_api.h"
#include "test_helpers.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *names[] = { "read1" };
static const char *seqs[]  = { "ACGTACGTACGTACGTACGTACGTACGTACGTACGTACGTAC" };

int main(void) {
    const char *bad_index = "/tmp/bt2_test_bad_index";

    /* Create truncated index files */
    create_truncated_index(bad_index);

    bt2_align_config_t config;
    bt2_align_config_init(&config);
    config.index_path = bad_index;
    config.quiet = 1;

    int err;
    bt2_align_ctx_t *ctx = bt2_align_create(&config, &err);
    /* bt2_align_create should succeed (files exist) */
    assert(ctx != NULL);

    bt2_input_t input;
    bt2_input_init(&input);
    input.names = names;
    input.seqs = seqs;
    input.quals = NULL;
    input.n_reads = 1;

    bt2_align_output_t *output = NULL;
    int rc = bt2_align_run(ctx, &input, &output, NULL);

    /* The critical assertion: we got an error code, not process death */
    printf("test_error_no_exit: bt2_align_run returned %d (%s)\n",
           rc, bt2_strerror(rc));
    printf("test_error_no_exit: last_error = '%s'\n",
           bt2_align_last_error(ctx));
    assert(rc != BT2_OK);  /* must be an error */

    if (output) bt2_align_output_free(output);
    bt2_align_destroy(ctx);

    /* Clean up */
    remove_truncated_index(bad_index);

    printf("test_error_no_exit: PASSED\n");
    return 0;
}
