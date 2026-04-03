/*
 * Helper program: builds an index via the C API.
 * Usage: api_build <reference.fa> <output_base>
 */
#include "bt2_api.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <reference.fa> <output_base>\n", argv[0]);
        return 1;
    }

    bt2_build_config_t config;
    bt2_build_config_init(&config);
    const char *refs[] = { argv[1] };
    config.ref_paths = refs;
    config.n_ref_paths = 1;
    config.output_base = argv[2];
    config.quiet = 1;

    int err;
    bt2_build_ctx_t *ctx = bt2_build_create(&config, &err);
    if (!ctx) {
        fprintf(stderr, "bt2_build_create failed: %s\n", bt2_strerror(err));
        return 1;
    }

    bt2_build_stats_t stats;
    int rc = bt2_build_run(ctx, &stats);
    if (rc != BT2_OK) {
        fprintf(stderr, "bt2_build_run failed: %s (%s)\n",
                bt2_strerror(rc), bt2_build_last_error(ctx));
        bt2_build_destroy(ctx);
        return 1;
    }

    printf("Built index: elapsed=%lldms\n", (long long)stats.elapsed_ms);
    bt2_build_destroy(ctx);
    return 0;
}
