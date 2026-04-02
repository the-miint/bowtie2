/*
 * Test that the library links correctly and basic functions are callable.
 */

#include "bt2_api.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    const char *msg;

    /* bt2_strerror returns non-NULL, non-empty for all known error codes */
    msg = bt2_strerror(BT2_OK);
    assert(msg != NULL);
    assert(strlen(msg) > 0);

    msg = bt2_strerror(BT2_ERR_NOMEM);
    assert(msg != NULL);
    assert(strlen(msg) > 0);

    msg = bt2_strerror(BT2_ERR_INVALID_CONFIG);
    assert(msg != NULL);
    assert(strlen(msg) > 0);

    msg = bt2_strerror(BT2_ERR_INDEX);
    assert(msg != NULL);
    assert(strlen(msg) > 0);

    msg = bt2_strerror(BT2_ERR_INPUT);
    assert(msg != NULL);
    assert(strlen(msg) > 0);

    msg = bt2_strerror(BT2_ERR_INTERNAL);
    assert(msg != NULL);
    assert(strlen(msg) > 0);

    /* Unknown error code also returns something */
    msg = bt2_strerror(-999);
    assert(msg != NULL);
    assert(strlen(msg) > 0);

    /* bt2_align_last_error with NULL returns empty string */
    msg = bt2_align_last_error(NULL);
    assert(msg != NULL);
    assert(strlen(msg) == 0);

    /* bt2_align_output_free is safe with NULL */
    bt2_align_output_free(NULL);

    /* bt2_align_destroy is safe with NULL */
    bt2_align_destroy(NULL);

    printf("test_link: PASSED\n");
    return 0;
}
