/*
 * Test that a context can be reused for multiple alignment runs.
 */
#include "bt2_api.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    bt2_align_config_t config;
    bt2_align_config_init(&config);
    config.index_path = "example/index/lambda_virus";
    config.quiet = 1;

    int err;
    bt2_align_ctx_t *ctx = bt2_align_create(&config, &err);
    assert(ctx != NULL);

    /* First run: longreads */
    const char *inputs1[] = {"example/reads/longreads.fq"};
    bt2_align_output_t *output1 = NULL;
    int rc = bt2_align_run_files(ctx, inputs1, 1, NULL, 0, &output1, NULL);
    assert(rc == BT2_OK);
    assert(output1 != NULL);
    size_t n1 = output1->n_records;
    assert(n1 > 0);

    /* Second run: paired reads (different input type, same context) */
    const char *m1[] = {"example/reads/reads_1.fq"};
    const char *m2[] = {"example/reads/reads_2.fq"};
    bt2_align_output_t *output2 = NULL;
    rc = bt2_align_run_files(ctx, m1, 1, m2, 1, &output2, NULL);
    assert(rc == BT2_OK);
    assert(output2 != NULL);
    size_t n2 = output2->n_records;
    assert(n2 > 0);

    /* Results should be different (different inputs) */
    assert(n1 != n2 || output1->flag[0] != output2->flag[0]);

    printf("test_context_reuse: run1=%zu records, run2=%zu records\n", n1, n2);

    bt2_align_output_free(output1);
    bt2_align_output_free(output2);
    bt2_align_destroy(ctx);

    printf("test_context_reuse: PASSED\n");
    return 0;
}
