/*
 * Reproducer for GPL-boundary macOS aarch64 SIGSEGV.
 *
 * See localdocs/ISSUE-bowtie2-sigsegv-macos-aarch64.md. Crashes on macOS
 * aarch64 with all-N input and no_unal=true. Run on Linux (with or without
 * ASan) to rule out / confirm a latent memory bug.
 */
#include "bt2_api.h"
#include "test_helpers.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(void) {
    const char *idx_base = "/tmp/bt2_test_repro_no_unal_all_n";
    cleanup_index(idx_base);

    /* 1. Build a small synthetic index matching the issue report */
    {
        /* Write reference to a temp FASTA */
        const char *ref_path = "/tmp/bt2_test_repro_ref.fa";
        FILE *f = fopen(ref_path, "w");
        if (!f) { perror("fopen ref"); return 1; }
        fputs(">ref1\n", f);
        fputs("ACGTACGTACGTACGTACGTACGTACGTACGTACGTACGTACGTACGTACGT\n", f);
        fputs("AAACCCGGGTTTAAACCCGGGTTTAAACCCGGGTTTAAACCCGGGTTTAAACCCGGG\n", f);
        fputs("TTTAAACCCGGGTTTAAACCCGGGTTTAAACCCGGGTTTAAACCCGGGTTTAAACCG\n", f);
        fputs("GCTAGCTAGCTAGCTAGCTAGCTAGCTAGCTAGCTAGCTAGCTAGCTAG\n", f);
        fclose(f);

        bt2_build_config_t bconfig;
        bt2_build_config_init(&bconfig);
        const char *refs[] = { ref_path };
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
            fprintf(stderr, "FAIL: bt2_build_run rc=%d\n", rc);
            cleanup_index(idx_base);
            return 1;
        }
        remove(ref_path);
    }

    /* 2. Align an all-N read with no_unal=true */
    bt2_align_config_t config;
    bt2_align_config_init(&config);
    config.index_path = idx_base;
    config.no_unal = 1;
    config.seed = 42;
    config.quiet = 1;

    int err;
    bt2_align_ctx_t *ctx = bt2_align_create(&config, &err);
    if (!ctx) {
        fprintf(stderr, "FAIL: bt2_align_create: %s\n", bt2_strerror(err));
        cleanup_index(idx_base);
        return 1;
    }

    const char *name = "garbage";
    const char *seq  = "NNNNNNNNNNNNNNNNNNNNNNNNNNN";   /* 27 N's */
    const char *qual = "!!!!!!!!!!!!!!!!!!!!!!!!!!!";   /* lowest Phred */
    const char *names[] = { name };
    const char *seqs[]  = { seq };
    const char *quals[] = { qual };

    bt2_input_t input;
    bt2_input_init(&input);
    input.names = names;
    input.seqs  = seqs;
    input.quals = quals;
    input.n_reads = 1;

    bt2_align_output_t *output = NULL;
    bt2_align_stats_t stats;
    memset(&stats, 0, sizeof(stats));

    int rc = bt2_align_run(ctx, &input, &output, &stats);
    if (rc != BT2_OK) {
        fprintf(stderr, "FAIL: bt2_align_run rc=%d: %s\n",
                rc, bt2_align_last_error(ctx));
        bt2_align_destroy(ctx);
        cleanup_index(idx_base);
        return 1;
    }
    if (!output) {
        fprintf(stderr, "FAIL: output is NULL\n");
        bt2_align_destroy(ctx);
        cleanup_index(idx_base);
        return 1;
    }

    printf("repro: n_records=%zu n_reads=%lld n_aligned=%lld\n",
           output->n_records,
           (long long)stats.n_reads,
           (long long)stats.n_aligned);

    /* With no_unal=true and a non-aligning read, we expect 0 records. */
    if (output->n_records != 0) {
        fprintf(stderr, "NOTE: expected 0 records, got %zu\n",
                output->n_records);
    }

    bt2_align_output_free(output);
    bt2_align_destroy(ctx);
    cleanup_index(idx_base);

    printf("test_repro_no_unal_all_n: PASSED\n");
    return 0;
}
