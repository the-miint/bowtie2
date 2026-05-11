/*
 * v0.3 optional-tag sanity test: exercises tag_ys/tag_xn/tag_xm/tag_xo/tag_xg.
 *
 * Coverage:
 *   - ABI: struct_size == sizeof(bt2_align_output_t) after appending v0.3
 *     fields, and all five new pointer fields are non-NULL.
 *   - Aligned records: tag_xn/xm/xo/xg are non-negative and tag_xg >= tag_xo
 *     (every gap open contributes at least one extension).
 *   - Unaligned record: tag_xn/xm/xo/xg are all 0 (the default sentinel).
 *   - Single-end mode: tag_ys is INT32_MIN for every record (no mate).
 *   - Paired-end mode: at least one mate pair satisfies the cross-mate
 *     parity invariant mate1.YS == mate2.AS && mate2.YS == mate1.AS.
 *
 * Coverage gap (intentional): tag_xn > 0 is not exercised here. The
 * lambda_virus reference has zero N bases, so refNs() is always 0 on the
 * example fixtures. Exercising the nonzero path would require building a
 * synthetic N-containing index — significant infrastructure for one
 * assertion when the implementation is `(int32_t)rs->refNs()` (single
 * trivial cast). The test_align_vs_native.py parity check would still
 * catch any divergence wholesale if XN ever appeared.
 */
#include "bt2_api.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

/* First three sequences align to lambda_virus; the fourth (polyA-40) does
   not — lambda's reference has no 40-bp A homopolymer, so this read fails
   to clear the default end-to-end score-min threshold. */
static const char *NAMES[] = {
    "aligned_1",
    "aligned_2",
    "aligned_3",
    "polya_unaligned",
};
static const char *SEQS[] = {
    "AGCTTTTCATTCTGACTGCAACGGGCAATATGTCTCTGTGT",
    "TTTAAATATGGTCTGATGATCTGGTTTATTGTGTCTTCTGA",
    "GCGATCTGTCTATTTCGTTCATCCATAGTTGCCTGACTCCC",
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",  /* 41 A's */
};
static const char *QUALS[] = {
    "IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII",
    "IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII",
    "IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII",
    "IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII",
};

#define SAM_FLAG_UNMAPPED 0x4

static void check_single_end(void) {
    bt2_align_config_t config;
    bt2_align_config_init(&config);
    config.index_path = "example/index/lambda_virus";
    config.quiet = 1;

    int err;
    bt2_align_ctx_t *ctx = bt2_align_create(&config, &err);
    assert(ctx != NULL && err == BT2_OK);

    bt2_input_t input;
    bt2_input_init(&input);
    input.names = NAMES;
    input.seqs  = SEQS;
    input.quals = QUALS;
    input.n_reads = 4;

    bt2_align_output_t *output = NULL;
    int rc = bt2_align_run(ctx, &input, &output, NULL);
    assert(rc == BT2_OK);
    assert(output != NULL);

    /* ABI check */
    assert(output->struct_size == sizeof(bt2_align_output_t));
    assert(output->n_records == 4);

    /* All five new pointer fields populated */
    assert(output->tag_ys != NULL);
    assert(output->tag_xn != NULL);
    assert(output->tag_xm != NULL);
    assert(output->tag_xo != NULL);
    assert(output->tag_xg != NULL);

    int saw_unaligned = 0;
    int saw_aligned   = 0;
    for (size_t i = 0; i < output->n_records; i++) {
        /* Single-end: YS is INT32_MIN for every record */
        assert(output->tag_ys[i] == INT_MIN);

        if (output->flag[i] & SAM_FLAG_UNMAPPED) {
            /* Sentinel default for unaligned record */
            assert(output->tag_xn[i] == 0);
            assert(output->tag_xm[i] == 0);
            assert(output->tag_xo[i] == 0);
            assert(output->tag_xg[i] == 0);
            saw_unaligned = 1;
        } else {
            /* Aligned: counts are non-negative and gap extensions >= opens */
            assert(output->tag_xn[i] >= 0);
            assert(output->tag_xm[i] >= 0);
            assert(output->tag_xo[i] >= 0);
            assert(output->tag_xg[i] >= 0);
            assert(output->tag_xg[i] >= output->tag_xo[i]);
            saw_aligned = 1;
        }
    }
    assert(saw_aligned);
    assert(saw_unaligned);

    bt2_align_output_free(output);
    bt2_align_destroy(ctx);
    printf("test_align_tag_fields[single-end]: 4 records, sentinels OK\n");
}

static void check_paired_end(void) {
    bt2_align_config_t config;
    bt2_align_config_init(&config);
    config.index_path = "example/index/lambda_virus";
    config.quiet = 1;

    int err;
    bt2_align_ctx_t *ctx = bt2_align_create(&config, &err);
    assert(ctx != NULL && err == BT2_OK);

    const char *m1[] = {"example/reads/reads_1.fq"};
    const char *m2[] = {"example/reads/reads_2.fq"};
    bt2_align_output_t *output = NULL;
    int rc = bt2_align_run_files(ctx, m1, 1, m2, 1, &output, NULL);
    assert(rc == BT2_OK && output != NULL);

    assert(output->struct_size == sizeof(bt2_align_output_t));
    assert(output->tag_ys != NULL);
    assert(output->tag_as != NULL);

    /* Cross-mate YS == AS parity invariant.
       Records from paired-end alignment carry the same rdid for both mates,
       and the columnar sink stable-sorts by rdid with mate1 appended before
       mate2 — so paired-mate records appear as adjacent (i, i+1) entries
       with identical QNAMEs (the /1, /2 suffix is stripped). For each such
       pair where both mates aligned, mate1's YS (the mate's score) must
       equal mate2's AS, and vice versa. This is the canonical YS parity
       property and holds in both end-to-end and local alignment modes. */
    size_t n = output->n_records;
    int checked_pairs = 0;
    for (size_t i = 0; i + 1 < n; i++) {
        if (strcmp(output->qname[i], output->qname[i+1]) != 0) continue;
        if (output->flag[i]   & SAM_FLAG_UNMAPPED) continue;
        if (output->flag[i+1] & SAM_FLAG_UNMAPPED) continue;

        assert(output->tag_ys[i]   != INT_MIN);
        assert(output->tag_ys[i+1] != INT_MIN);
        assert(output->tag_ys[i]   == output->tag_as[i+1]);
        assert(output->tag_ys[i+1] == output->tag_as[i]);

        checked_pairs++;
        i++; /* skip the mate we just paired */
    }
    assert(checked_pairs > 0); /* lambda_virus example reads have concordant pairs */

    size_t n_records = output->n_records;
    bt2_align_output_free(output);
    bt2_align_destroy(ctx);
    printf("test_align_tag_fields[paired-end]: %zu records, %d cross-mate YS pairs verified\n",
           n_records, checked_pairs);
}

int main(void) {
    check_single_end();
    check_paired_end();
    printf("test_align_tag_fields: PASSED\n");
    return 0;
}
