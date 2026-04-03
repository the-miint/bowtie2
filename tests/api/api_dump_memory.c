/*
 * Helper program: reads a FASTQ file into memory arrays, aligns via
 * bt2_align_run() (in-memory API), and dumps results as TSV to stdout.
 * Output format matches api_dump for comparison.
 *
 * Usage: api_dump_memory <index> <reads.fq> [reads2.fq]
 */
#include "bt2_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Simple FASTQ reader: reads all records into parallel arrays.
   Caller frees names, seqs, quals, and the backing buffer. */
static int read_fastq(const char *path,
                      char ***names_out, char ***seqs_out, char ***quals_out,
                      size_t *n_out, char **backing_out) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "Cannot open %s\n", path); return -1; }

    /* Read entire file */
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); *n_out = 0; return 0; }

    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return -1; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';

    /* Count records (each has 4 lines: @name, seq, +, qual) */
    size_t n_lines = 0;
    for (size_t i = 0; i < rd; i++)
        if (buf[i] == '\n') n_lines++;
    size_t capacity = (n_lines / 4) + 1;

    char **names = (char **)malloc(capacity * sizeof(char *));
    char **seqs  = (char **)malloc(capacity * sizeof(char *));
    char **quals = (char **)malloc(capacity * sizeof(char *));
    if (!names || !seqs || !quals) {
        free(buf); free(names); free(seqs); free(quals);
        return -1;
    }

    size_t n = 0;
    char *p = buf;
    while (*p) {
        /* @name */
        if (*p != '@') break;
        p++; /* skip '@' */
        char *name_start = p;
        while (*p && *p != '\n') p++;
        if (*p) *p++ = '\0';
        /* Trim name at first space (FASTQ convention) */
        char *sp = strchr(name_start, ' ');
        if (sp) *sp = '\0';

        /* sequence */
        char *seq_start = p;
        while (*p && *p != '\n') p++;
        if (*p) *p++ = '\0';

        /* + line */
        while (*p && *p != '\n') p++;
        if (*p) p++;

        /* quality */
        char *qual_start = p;
        while (*p && *p != '\n') p++;
        if (*p) *p++ = '\0';

        if (n >= capacity) {
            size_t new_cap = capacity * 2;
            char **tmp_n = (char **)realloc(names, new_cap * sizeof(char *));
            char **tmp_s = (char **)realloc(seqs,  new_cap * sizeof(char *));
            char **tmp_q = (char **)realloc(quals, new_cap * sizeof(char *));
            if (!tmp_n || !tmp_s || !tmp_q) {
                free(tmp_n ? tmp_n : names);
                free(tmp_s ? tmp_s : seqs);
                free(tmp_q ? tmp_q : quals);
                free(buf);
                return -1;
            }
            names = tmp_n;
            seqs  = tmp_s;
            quals = tmp_q;
            capacity = new_cap;
        }

        names[n] = name_start;
        seqs[n]  = seq_start;
        quals[n] = qual_start;
        n++;
    }

    *names_out = names;
    *seqs_out  = seqs;
    *quals_out = quals;
    *n_out = n;
    *backing_out = buf;
    return 0;
}

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

    /* Read mate 1 */
    char **names1 = NULL, **seqs1 = NULL, **quals1 = NULL;
    size_t n1 = 0;
    char *backing1 = NULL;
    if (read_fastq(argv[2], &names1, &seqs1, &quals1, &n1, &backing1) != 0) {
        fprintf(stderr, "Failed to read %s\n", argv[2]);
        bt2_align_destroy(ctx);
        return 1;
    }

    /* Read mate 2 if provided */
    char **names2 = NULL, **seqs2 = NULL, **quals2 = NULL;
    size_t n2 = 0;
    char *backing2 = NULL;
    int paired = (argc > 3);
    if (paired) {
        if (read_fastq(argv[3], &names2, &seqs2, &quals2, &n2, &backing2) != 0) {
            fprintf(stderr, "Failed to read %s\n", argv[3]);
            free(names1); free(seqs1); free(quals1); free(backing1);
            bt2_align_destroy(ctx);
            return 1;
        }
    }

    bt2_input_t input;
    bt2_input_init(&input);
    input.names = (const char **)names1;
    input.seqs  = (const char **)seqs1;
    input.quals = (const char **)quals1;
    input.n_reads = n1;
    if (paired) {
        input.names2 = (const char **)names2;
        input.seqs2  = (const char **)seqs2;
        input.quals2 = (const char **)quals2;
        input.n_reads2 = n2;
    }

    bt2_align_output_t *output = NULL;
    int rc = bt2_align_run(ctx, &input, &output, NULL);
    if (rc != BT2_OK) {
        fprintf(stderr, "bt2_align_run failed: %s (%s)\n",
                bt2_strerror(rc), bt2_align_last_error(ctx));
        free(names1); free(seqs1); free(quals1); free(backing1);
        free(names2); free(seqs2); free(quals2); free(backing2);
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
        printf("\t%d", output->tag_as[i]);
        printf("\t%d", output->tag_nm[i]);
        printf("\t%s", output->tag_md[i] ? output->tag_md[i] : "");
        printf("\t%s", output->tag_yt[i] ? output->tag_yt[i] : "");
        printf("\n");
    }

    bt2_align_output_free(output);
    free(names1); free(seqs1); free(quals1); free(backing1);
    free(names2); free(seqs2); free(quals2); free(backing2);
    bt2_align_destroy(ctx);
    return 0;
}
