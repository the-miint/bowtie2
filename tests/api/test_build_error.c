/*
 * Phase 6b test: nonexistent FASTA returns error, not crash.
 */
#include "bt2_api.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    bt2_build_config_t config;
    bt2_build_config_init(&config);
    const char *refs[] = { "/nonexistent/path/to/reference.fa" };
    config.ref_paths = refs;
    config.n_ref_paths = 1;
    config.output_base = "/tmp/bt2_test_build_error";

    int err;
    bt2_build_ctx_t *ctx = bt2_build_create(&config, &err);
    /* Should fail at create time — file doesn't exist */
    if (ctx != NULL) {
        fprintf(stderr, "FAIL: expected NULL from bt2_build_create for bad ref\n");
        bt2_build_destroy(ctx);
        return 1;
    }
    if (err != BT2_ERR_INPUT) {
        fprintf(stderr, "FAIL: expected BT2_ERR_INPUT, got %d\n", err);
        return 1;
    }

    printf("test_build_error: PASSED (got expected error %d: %s)\n",
           err, bt2_strerror(err));
    return 0;
}
