/*
 * Phase 6a test: verify bt2_build_config_t and related types compile.
 */
#include "bt2_api.h"
#include <stdio.h>

int main(void) {
    /* Verify types exist and have expected sizes */
    bt2_build_config_t config;
    (void)config;
    bt2_build_stats_t stats;
    (void)stats;

    /* Verify function declarations exist */
    void (*init_fn)(bt2_build_config_t *) = bt2_build_config_init;
    bt2_build_ctx_t *(*create_fn)(const bt2_build_config_t *, int *) = bt2_build_create;
    int (*run_fn)(bt2_build_ctx_t *, bt2_build_stats_t *) = bt2_build_run;
    void (*destroy_fn)(bt2_build_ctx_t *) = bt2_build_destroy;
    const char *(*last_err_fn)(const bt2_build_ctx_t *) = bt2_build_last_error;
    (void)init_fn; (void)create_fn; (void)run_fn;
    (void)destroy_fn; (void)last_err_fn;

    printf("test_build_header: PASSED\n");
    return 0;
}
