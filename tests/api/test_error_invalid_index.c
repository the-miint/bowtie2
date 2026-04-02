/*
 * Test error handling for invalid index path.
 */
#include "bt2_api.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    bt2_align_config_t config;
    bt2_align_config_init(&config);
    config.index_path = "/nonexistent/path/to/index";

    int err = BT2_OK;
    bt2_align_ctx_t *ctx = bt2_align_create(&config, &err);

    /* Should fail at create time since index files don't exist */
    assert(ctx == NULL);
    assert(err == BT2_ERR_INDEX);

    /* bt2_strerror should give a useful message */
    const char *msg = bt2_strerror(err);
    assert(msg != NULL);
    assert(strlen(msg) > 0);

    printf("test_error_invalid_index: PASSED (err=%d: %s)\n", err, msg);
    return 0;
}
