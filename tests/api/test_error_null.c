/*
 * Test NULL-safety of all public API functions.
 */
#include "bt2_api.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    /* bt2_align_run_files with NULL context */
    int rc = bt2_align_run_files(NULL, NULL, 0, NULL, 0, NULL, NULL);
    assert(rc != BT2_OK); /* should return error, not crash */

    /* bt2_align_destroy with NULL */
    bt2_align_destroy(NULL); /* should not crash */

    /* bt2_align_output_free with NULL */
    bt2_align_output_free(NULL); /* should not crash */

    /* bt2_align_last_error with NULL */
    const char *msg = bt2_align_last_error(NULL);
    assert(msg != NULL);
    assert(strlen(msg) == 0);

    /* bt2_align_config_init with NULL */
    bt2_align_config_init(NULL); /* should not crash */

    /* bt2_align_create with NULL */
    int err = BT2_OK;
    bt2_align_ctx_t *ctx = bt2_align_create(NULL, &err);
    assert(ctx == NULL);
    assert(err == BT2_ERR_INVALID_CONFIG);

    printf("test_error_null: PASSED\n");
    return 0;
}
