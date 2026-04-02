/*
 * Test that bt2_align_config_init sets all fields to expected defaults
 * and that bt2_align_create validates config properly.
 */

#include "bt2_api.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    bt2_align_config_t config;
    int err;

    /* NULL config_init is a safe no-op */
    bt2_align_config_init(NULL);

    /* Zero-fill with 0xFF to detect any fields config_init misses */
    memset(&config, 0xFF, sizeof(config));

    bt2_align_config_init(&config);

    /* struct_size must equal sizeof the struct */
    assert(config.struct_size == sizeof(bt2_align_config_t));

    /* index_path defaults to NULL (caller must set) */
    assert(config.index_path == NULL);

    /* seed defaults to 0 */
    assert(config.seed == 0);

    /* nthreads defaults to 1 (single-threaded per GPL-boundary guidance) */
    assert(config.nthreads == 1);

    /* preset defaults to SENSITIVE */
    assert(config.preset == BT2_PRESET_SENSITIVE);

    /* local_align defaults to 0 (end-to-end) */
    assert(config.local_align == 0);

    /* quiet defaults to 1 (library should be quiet by default) */
    assert(config.quiet == 1);

    /* log callback defaults to NULL */
    assert(config.log_fn == NULL);
    assert(config.log_user_data == NULL);

    /* --- bt2_align_create validation tests --- */

    /* NULL config returns NULL with BT2_ERR_INVALID_CONFIG */
    err = BT2_OK;
    assert(bt2_align_create(NULL, &err) == NULL);
    assert(err == BT2_ERR_INVALID_CONFIG);

    /* Config with no index_path returns NULL */
    err = BT2_OK;
    assert(bt2_align_create(&config, &err) == NULL);
    assert(err == BT2_ERR_INVALID_CONFIG);

    /* Config with wrong struct_size returns NULL */
    config.index_path = "some/index";
    config.struct_size = 1;  /* wrong size */
    err = BT2_OK;
    assert(bt2_align_create(&config, &err) == NULL);
    assert(err == BT2_ERR_INVALID_CONFIG);

    /* NULL error_out is safe (doesn't crash) */
    config.struct_size = sizeof(bt2_align_config_t);
    config.index_path = NULL;
    assert(bt2_align_create(&config, NULL) == NULL);

    printf("test_config_init: PASSED\n");
    return 0;
}
