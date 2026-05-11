/*
 * Helper program: runs alignment via the C API and dumps results as TSV
 * to stdout for comparison with native bowtie2 SAM output.
 *
 * Usage: api_dump <index> <reads.fq> [reads2.fq] [options...]
 *
 * Options mirror bt2_align_config_t fields:
 *   --k N  --report-all  --trim5 N  --trim3 N
 *   --ma N  --mp N  --np N  --rdg-open N  --rdg-extend N
 *   --rfg-open N  --rfg-extend N  --score-min STR
 *   --min-insert N  --max-insert N  --mate-orient FR|RF|FF
 *   --no-mixed  --no-discordant  --dovetail  --no-contain  --no-overlap
 *   --nofw  --norc
 *   --seed-mm N  --seed-len N  --max-dp-fail N  --max-seed-rounds N
 *   --no-unal  --xeq  --rg-id STR
 *   --ignore-quals  --reorder  --local  --seed N
 *
 * Output: one TSV line per record:
 *   QNAME\tFLAG\tRNAME\tPOS\tMAPQ\tCIGAR\tRNEXT\tPNEXT\tTLEN\tSEQ\tQUAL\t
 *   AS\tNM\tMD\tYT\tYS\tXN\tXM\tXO\tXG
 */
#include "bt2_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <index> <reads.fq> [reads2.fq] [options...]\n", argv[0]);
        return 1;
    }

    bt2_align_config_t config;
    bt2_align_config_init(&config);
    config.index_path = argv[1];
    config.quiet = 1;

    /* Determine reads files and where options start.
       argv[3] is reads2 if it exists and doesn't start with '--'. */
    const char *reads1 = argv[2];
    const char *reads2 = NULL;
    int opt_start = 3;

    if (argc > 3 && strncmp(argv[3], "--", 2) != 0) {
        reads2 = argv[3];
        opt_start = 4;
    }

    /* Parse option flags */
    for (int i = opt_start; i < argc; i++) {
        if (strcmp(argv[i], "--k") == 0 && i + 1 < argc)
            config.k = atoi(argv[++i]);
        else if (strcmp(argv[i], "--report-all") == 0)
            config.report_all = 1;
        else if (strcmp(argv[i], "--trim5") == 0 && i + 1 < argc)
            config.trim5 = atoi(argv[++i]);
        else if (strcmp(argv[i], "--trim3") == 0 && i + 1 < argc)
            config.trim3 = atoi(argv[++i]);
        else if (strcmp(argv[i], "--ma") == 0 && i + 1 < argc)
            config.match_bonus = atoi(argv[++i]);
        else if (strcmp(argv[i], "--mp") == 0 && i + 1 < argc)
            config.mismatch_penalty = atoi(argv[++i]);
        else if (strcmp(argv[i], "--np") == 0 && i + 1 < argc)
            config.n_penalty = atoi(argv[++i]);
        else if (strcmp(argv[i], "--rdg-open") == 0 && i + 1 < argc)
            config.read_gap_open = atoi(argv[++i]);
        else if (strcmp(argv[i], "--rdg-extend") == 0 && i + 1 < argc)
            config.read_gap_extend = atoi(argv[++i]);
        else if (strcmp(argv[i], "--rfg-open") == 0 && i + 1 < argc)
            config.ref_gap_open = atoi(argv[++i]);
        else if (strcmp(argv[i], "--rfg-extend") == 0 && i + 1 < argc)
            config.ref_gap_extend = atoi(argv[++i]);
        else if (strcmp(argv[i], "--score-min") == 0 && i + 1 < argc)
            config.score_min = argv[++i];
        else if (strcmp(argv[i], "--min-insert") == 0 && i + 1 < argc)
            config.min_insert = atoi(argv[++i]);
        else if (strcmp(argv[i], "--max-insert") == 0 && i + 1 < argc)
            config.max_insert = atoi(argv[++i]);
        else if (strcmp(argv[i], "--mate-orient") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "RF") == 0) config.mate_orientation = BT2_MATE_RF;
            else if (strcmp(argv[i], "FF") == 0) config.mate_orientation = BT2_MATE_FF;
            else config.mate_orientation = BT2_MATE_FR;
        }
        else if (strcmp(argv[i], "--no-mixed") == 0)
            config.no_mixed = 1;
        else if (strcmp(argv[i], "--no-discordant") == 0)
            config.no_discordant = 1;
        else if (strcmp(argv[i], "--dovetail") == 0)
            config.dovetail = 1;
        else if (strcmp(argv[i], "--no-contain") == 0)
            config.no_contain = 1;
        else if (strcmp(argv[i], "--no-overlap") == 0)
            config.no_overlap = 1;
        else if (strcmp(argv[i], "--nofw") == 0)
            config.nofw = 1;
        else if (strcmp(argv[i], "--norc") == 0)
            config.norc = 1;
        else if (strcmp(argv[i], "--seed-mm") == 0 && i + 1 < argc)
            config.seed_mismatches = atoi(argv[++i]);
        else if (strcmp(argv[i], "--seed-len") == 0 && i + 1 < argc)
            config.seed_length = atoi(argv[++i]);
        else if (strcmp(argv[i], "--max-dp-fail") == 0 && i + 1 < argc)
            config.max_dp_failures = atoi(argv[++i]);
        else if (strcmp(argv[i], "--max-seed-rounds") == 0 && i + 1 < argc)
            config.max_seed_rounds = atoi(argv[++i]);
        else if (strcmp(argv[i], "--no-unal") == 0)
            config.no_unal = 1;
        else if (strcmp(argv[i], "--xeq") == 0)
            config.xeq = 1;
        else if (strcmp(argv[i], "--rg-id") == 0 && i + 1 < argc)
            config.rg_id = argv[++i];
        else if (strcmp(argv[i], "--ignore-quals") == 0)
            config.ignore_quals = 1;
        else if (strcmp(argv[i], "--reorder") == 0)
            config.reorder = 1;
        else if (strcmp(argv[i], "--local") == 0)
            config.local_align = 1;
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
            config.seed = atoll(argv[++i]);
        else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    int err;
    bt2_align_ctx_t *ctx = bt2_align_create(&config, &err);
    if (!ctx) {
        fprintf(stderr, "bt2_align_create failed: %s\n", bt2_strerror(err));
        return 1;
    }

    const char *m1[] = {reads1};
    const char *m2[] = {reads2};
    int n_m2 = reads2 ? 1 : 0;
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
        /* v0.3 tags */
        printf("\t%d", output->tag_ys[i]);
        printf("\t%d", output->tag_xn[i]);
        printf("\t%d", output->tag_xm[i]);
        printf("\t%d", output->tag_xo[i]);
        printf("\t%d", output->tag_xg[i]);
        printf("\n");
    }

    bt2_align_output_free(output);
    bt2_align_destroy(ctx);
    return 0;
}
