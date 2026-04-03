/*
 * Phase 6a test: verify bt2_build_config_init sets correct defaults.
 */
#include "bt2_api.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    bt2_build_config_t config;
    memset(&config, 0xFF, sizeof(config)); /* poison */
    bt2_build_config_init(&config);

    if (config.struct_size != sizeof(bt2_build_config_t)) {
        fprintf(stderr, "FAIL: struct_size=%zu expected=%zu\n",
                config.struct_size, sizeof(bt2_build_config_t));
        return 1;
    }
    if (config.ref_paths != NULL) { fprintf(stderr, "FAIL: ref_paths\n"); return 1; }
    if (config.n_ref_paths != 0) { fprintf(stderr, "FAIL: n_ref_paths\n"); return 1; }
    if (config.output_base != NULL) { fprintf(stderr, "FAIL: output_base\n"); return 1; }
    if (config.nthreads != 1) { fprintf(stderr, "FAIL: nthreads=%d\n", config.nthreads); return 1; }
    if (config.seed != 0) { fprintf(stderr, "FAIL: seed\n"); return 1; }
    if (config.offrate != 4) { fprintf(stderr, "FAIL: offrate=%d\n", config.offrate); return 1; }
    if (config.packed != 0) { fprintf(stderr, "FAIL: packed\n"); return 1; }
    if (config.quiet != 1) { fprintf(stderr, "FAIL: quiet=%d\n", config.quiet); return 1; }
    if (config.log_fn != NULL) { fprintf(stderr, "FAIL: log_fn\n"); return 1; }

    /* NULL is safe */
    bt2_build_config_init(NULL);

    printf("test_build_config_init: PASSED\n");
    return 0;
}
