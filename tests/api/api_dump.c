/*
 * Helper program: runs alignment via the C API and dumps results as TSV
 * to stdout for comparison with native bowtie2 SAM output.
 *
 * Usage: api_dump <index> <reads.fq> [reads2.fq]
 * Output: one TSV line per record:
 *   QNAME\tFLAG\tRNAME\tPOS\tMAPQ\tCIGAR\tRNEXT\tPNEXT\tTLEN\tSEQ\tQUAL\tAS\tNM\tMD\tYT
 */
#include "bt2_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <index> <reads.fq> [reads2.fq]\n", argv[0]);
        return 1;
    }

    bt2_align_config_t config;
    bt2_align_config_init(&config);
    config.index_path = argv[1];
    config.quiet = 1;

    int err;
    bt2_align_ctx_t *ctx = bt2_align_create(&config, &err);
    if (!ctx) {
        fprintf(stderr, "bt2_align_create failed: %s\n", bt2_strerror(err));
        return 1;
    }

    const char *m1[] = {argv[2]};
    const char *m2[] = {argc > 3 ? argv[3] : NULL};
    int n_m2 = (argc > 3) ? 1 : 0;
    bt2_align_output_t *output = NULL;

    int rc = bt2_align_run_files(ctx, m1, 1,
                                 n_m2 > 0 ? m2 : NULL, (size_t)n_m2,
                                 &output, NULL);
    if (rc != BT2_OK) {
        fprintf(stderr, "bt2_align_run_files failed: %s\n", bt2_strerror(rc));
        bt2_align_destroy(ctx);
        return 1;
    }

    for (size_t i = 0; i < output->n_records; i++) {
        printf("%s\t%d\t%s\t%lld\t%d\t%s\t%s\t%lld\t%lld\t%s\t%s",
               output->qname[i],
               output->flag[i],
               output->rname[i],
               (long long)output->pos[i],
               (int)output->mapq[i],
               output->cigar[i],
               output->rnext[i],
               (long long)output->pnext[i],
               (long long)output->tlen[i],
               output->seq[i],
               output->qual[i]);
        /* Optional tags */
        printf("\t%d", output->tag_as[i]);
        printf("\t%d", output->tag_nm[i]);
        printf("\t%s", output->tag_md[i] ? output->tag_md[i] : "");
        printf("\t%s", output->tag_yt[i] ? output->tag_yt[i] : "");
        printf("\n");
    }

    bt2_align_output_free(output);
    bt2_align_destroy(ctx);
    return 0;
}
