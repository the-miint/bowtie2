/*
 * Phase 7c integration test: build an index via bt2_build_run, then
 * align reads against it via bt2_align_run, all in one process.
 * Exercises both APIs sequentially with correct lifecycle management.
 */
#include "bt2_api.h"
#include "test_helpers.h"
#include <stdio.h>
#include <string.h>

static const char *names[] = { "integ_read_1", "integ_read_2" };
/* Real subsequences from lambda_virus.fa (pos 100 and 500) */
static const char *seqs[] = {
    "CTCTGAAAAGAAAGGAAACGACAGGTGCTGAAAGCGAGGCT",
    "ACTCCGCTGAAGTGGTGGAAACCGCATTCTGTACTTTCGTG",
};

int main(void) {
    const char *idx_base = "/tmp/bt2_test_build_then_align";
    cleanup_index(idx_base);

    /* Step 1: Build index from reference FASTA */
    {
        bt2_build_config_t bconfig;
        bt2_build_config_init(&bconfig);
        const char *refs[] = { "example/reference/lambda_virus.fa" };
        bconfig.ref_paths = refs;
        bconfig.n_ref_paths = 1;
        bconfig.output_base = idx_base;
        bconfig.quiet = 1;

        int err;
        bt2_build_ctx_t *bctx = bt2_build_create(&bconfig, &err);
        if (!bctx) {
            fprintf(stderr, "FAIL: bt2_build_create: %s\n", bt2_strerror(err));
            return 1;
        }

        bt2_build_stats_t bstats;
        int rc = bt2_build_run(bctx, &bstats);
        bt2_build_destroy(bctx);

        if (rc != BT2_OK) {
            fprintf(stderr, "FAIL: bt2_build_run returned %d\n", rc);
            cleanup_index(idx_base);
            return 1;
        }
        printf("Build: elapsed=%lldms\n", (long long)bstats.elapsed_ms);
    }

    /* Step 2: Align reads against the API-built index */
    {
        bt2_align_config_t aconfig;
        bt2_align_config_init(&aconfig);
        aconfig.index_path = idx_base;
        aconfig.quiet = 1;

        int err;
        bt2_align_ctx_t *actx = bt2_align_create(&aconfig, &err);
        if (!actx) {
            fprintf(stderr, "FAIL: bt2_align_create: %s\n", bt2_strerror(err));
            cleanup_index(idx_base);
            return 1;
        }

        bt2_input_t input;
        bt2_input_init(&input);
        input.names = names;
        input.seqs = seqs;
        input.quals = NULL;
        input.n_reads = 2;

        bt2_align_output_t *output = NULL;
        bt2_align_stats_t astats;
        memset(&astats, 0, sizeof(astats));

        int rc = bt2_align_run(actx, &input, &output, &astats);
        if (rc != BT2_OK) {
            fprintf(stderr, "FAIL: bt2_align_run returned %d: %s\n",
                    rc, bt2_align_last_error(actx));
            bt2_align_destroy(actx);
            cleanup_index(idx_base);
            return 1;
        }

        if (output->n_records != 2) {
            fprintf(stderr, "FAIL: expected 2 records, got %zu\n", output->n_records);
            bt2_align_output_free(output);
            bt2_align_destroy(actx);
            cleanup_index(idx_base);
            return 1;
        }

        printf("Align: %zu records, %lld reads, %lld aligned\n",
               output->n_records,
               (long long)astats.n_reads,
               (long long)astats.n_aligned);

        /* Verify output is well-formed and at least one read aligned */
        size_t i;
        int any_mapped = 0;
        for (i = 0; i < output->n_records; i++) {
            if (!output->qname[i] || !output->seq[i]) {
                fprintf(stderr, "FAIL: NULL field at record %zu\n", i);
                bt2_align_output_free(output);
                bt2_align_destroy(actx);
                cleanup_index(idx_base);
                return 1;
            }
            if (!(output->flag[i] & 0x4)) any_mapped = 1; /* not unmapped */
        }
        if (!any_mapped) {
            fprintf(stderr, "FAIL: no reads aligned against API-built index\n");
            bt2_align_output_free(output);
            bt2_align_destroy(actx);
            cleanup_index(idx_base);
            return 1;
        }

        bt2_align_output_free(output);
        bt2_align_destroy(actx);
    }

    cleanup_index(idx_base);
    printf("test_build_then_align: PASSED\n");
    return 0;
}
