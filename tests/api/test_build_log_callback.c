/*
 * Phase 6c test: verify log callback receives build messages.
 */
#include "bt2_api.h"
#include "test_helpers.h"
#include <stdio.h>
#include <string.h>

static int log_count = 0;

static void test_log_fn(void *user_data, int level, const char *msg) {
    (void)user_data;
    (void)level;
    (void)msg;
    log_count++;
}


int main(void) {
    const char *base = "/tmp/bt2_test_build_log";
    cleanup_index(base);
    log_count = 0;

    bt2_build_config_t config;
    bt2_build_config_init(&config);
    const char *refs[] = { "example/reference/lambda_virus.fa" };
    config.ref_paths = refs;
    config.n_ref_paths = 1;
    config.output_base = base;
    config.quiet = 0;  /* non-quiet: produces verbose build messages */
    config.log_fn = test_log_fn;

    int err;
    bt2_build_ctx_t *ctx = bt2_build_create(&config, &err);
    if (!ctx) { fprintf(stderr, "FAIL: create\n"); return 1; }

    int rc = bt2_build_run(ctx, NULL);
    if (rc != BT2_OK) {
        fprintf(stderr, "FAIL: run: %s\n", bt2_build_last_error(ctx));
        bt2_build_destroy(ctx);
        cleanup_index(base);
        return 1;
    }

    printf("test_build_log_callback: received %d log messages\n", log_count);
    if (log_count == 0) {
        fprintf(stderr, "FAIL: expected log messages with quiet=0 and log_fn set\n");
        bt2_build_destroy(ctx);
        cleanup_index(base);
        return 1;
    }

    bt2_build_destroy(ctx);
    cleanup_index(base);

    printf("test_build_log_callback: PASSED\n");
    return 0;
}
