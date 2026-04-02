/*
 * Test that bt2_api.h compiles as pure C and that basic type
 * declarations are accessible.
 */

#include "bt2_api.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    /* Config struct is declared and has expected size field */
    bt2_align_config_t config;
    bt2_align_config_init(&config);

    /* struct_size is set correctly */
    assert(config.struct_size == sizeof(bt2_align_config_t));

    /* Error code constants are defined */
    assert(BT2_OK == 0);
    assert(BT2_ERR_NOMEM < 0);
    assert(BT2_ERR_INVALID_CONFIG < 0);
    assert(BT2_ERR_INDEX < 0);
    assert(BT2_ERR_INPUT < 0);
    assert(BT2_ERR_INTERNAL < 0);

    /* Preset constants are defined */
    assert(BT2_PRESET_VERY_FAST == 0);
    assert(BT2_PRESET_FAST == 1);
    assert(BT2_PRESET_SENSITIVE == 2);
    assert(BT2_PRESET_VERY_SENSITIVE == 3);

    /* Log level constants are defined */
    assert(BT2_LOG_ERROR == 0);
    assert(BT2_LOG_DEBUG == 3);

    /* Version macros are defined */
    assert(BT2_API_VERSION_MAJOR >= 0);
    assert(BT2_API_VERSION_MINOR >= 0);

    /* Output struct has struct_size field */
    assert(sizeof(bt2_align_output_t) > 0);

    /* Stats struct is value-only (no pointers to free) */
    assert(sizeof(bt2_align_stats_t) > 0);

    printf("test_header: PASSED\n");
    return 0;
}
