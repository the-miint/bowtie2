/*
 * Phase 6a test: build an index from lambda_virus.fa via the API,
 * verify .bt2 files are created.
 */
#include "bt2_api.h"
#include "test_helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>



int main(void) {
    const char *output_base = "/tmp/bt2_test_build_api";
    cleanup_index(output_base);

    bt2_build_config_t config;
    bt2_build_config_init(&config);
    const char *refs[] = { "example/reference/lambda_virus.fa" };
    config.ref_paths = refs;
    config.n_ref_paths = 1;
    config.output_base = output_base;
    config.quiet = 1;

    int err;
    bt2_build_ctx_t *ctx = bt2_build_create(&config, &err);
    if (!ctx) {
        fprintf(stderr, "FAIL: bt2_build_create returned NULL: %s\n",
                bt2_strerror(err));
        return 1;
    }

    bt2_build_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    int rc = bt2_build_run(ctx, &stats);
    if (rc != BT2_OK) {
        fprintf(stderr, "FAIL: bt2_build_run returned %d: %s\n",
                rc, bt2_build_last_error(ctx));
        bt2_build_destroy(ctx);
        cleanup_index(output_base);
        return 1;
    }

    /* Verify all 6 .bt2 files exist */
    char path[512];
    const char *suffixes[] = {
        ".1.bt2", ".2.bt2", ".3.bt2", ".4.bt2", ".rev.1.bt2", ".rev.2.bt2"
    };
    int i;
    for (i = 0; i < 6; i++) {
        snprintf(path, sizeof(path), "%s%s", output_base, suffixes[i]);
        if (!file_exists(path)) {
            fprintf(stderr, "FAIL: missing %s\n", path);
            bt2_build_destroy(ctx);
            cleanup_index(output_base);
            return 1;
        }
    }

    /* Stats should be populated */
    if (stats.elapsed_ms < 0) {
        fprintf(stderr, "FAIL: elapsed_ms=%lld\n", (long long)stats.elapsed_ms);
        bt2_build_destroy(ctx);
        cleanup_index(output_base);
        return 1;
    }

    printf("test_build_basic: built index at %s, elapsed=%lldms\n",
           output_base, (long long)stats.elapsed_ms);

    bt2_build_destroy(ctx);
    cleanup_index(output_base);

    printf("test_build_basic: PASSED\n");
    return 0;
}
