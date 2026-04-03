/*
 * Phase 6b test: build two indexes sequentially (create, run, destroy, repeat).
 */
#include "bt2_api.h"
#include "test_helpers.h"
#include <stdio.h>
#include <string.h>



int main(void) {
    const char *base1 = "/tmp/bt2_test_reuse_1";
    const char *base2 = "/tmp/bt2_test_reuse_2";
    cleanup_index(base1);
    cleanup_index(base2);

    const char *refs[] = { "example/reference/lambda_virus.fa" };

    /* Build 1 */
    {
        bt2_build_config_t config;
        bt2_build_config_init(&config);
        config.ref_paths = refs;
        config.n_ref_paths = 1;
        config.output_base = base1;
        config.quiet = 1;

        int err;
        bt2_build_ctx_t *ctx = bt2_build_create(&config, &err);
        if (!ctx) { fprintf(stderr, "FAIL: create 1\n"); return 1; }

        int rc = bt2_build_run(ctx, NULL);
        if (rc != BT2_OK) {
            fprintf(stderr, "FAIL: run 1: %s\n", bt2_build_last_error(ctx));
            bt2_build_destroy(ctx);
            return 1;
        }
        bt2_build_destroy(ctx);

        char p[512];
        snprintf(p, sizeof(p), "%s.1.bt2", base1);
        if (!file_exists(p)) { fprintf(stderr, "FAIL: missing %s\n", p); return 1; }
    }

    /* Build 2 */
    {
        bt2_build_config_t config;
        bt2_build_config_init(&config);
        config.ref_paths = refs;
        config.n_ref_paths = 1;
        config.output_base = base2;
        config.quiet = 1;

        int err;
        bt2_build_ctx_t *ctx = bt2_build_create(&config, &err);
        if (!ctx) { fprintf(stderr, "FAIL: create 2\n"); return 1; }

        int rc = bt2_build_run(ctx, NULL);
        if (rc != BT2_OK) {
            fprintf(stderr, "FAIL: run 2: %s\n", bt2_build_last_error(ctx));
            bt2_build_destroy(ctx);
            return 1;
        }
        bt2_build_destroy(ctx);

        char p[512];
        snprintf(p, sizeof(p), "%s.1.bt2", base2);
        if (!file_exists(p)) { fprintf(stderr, "FAIL: missing %s\n", p); return 1; }
    }

    cleanup_index(base1);
    cleanup_index(base2);

    printf("test_build_context_reuse: PASSED\n");
    return 0;
}
