/*
 * Benchmark helper: measures API alignment performance.
 *
 * Reports JSON with context creation time, alignment time, record count.
 * Supports both file-based and in-memory paths, single and paired-end.
 *
 * Usage:
 *   bench_api file   <index> <reads.fq> [reads2.fq]  [iterations]
 *   bench_api memory <index> <reads.fq> [reads2.fq]  [iterations]
 */
#include "bt2_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* Simple FASTQ reader into parallel arrays */
static int read_fastq(const char *path,
                      char ***names_out, char ***seqs_out, char ***quals_out,
                      size_t *n_out, char **backing_out) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "Cannot open %s\n", path); return -1; }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); *n_out = 0; return 0; }

    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return -1; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';

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
        if (*p != '@') break;
        p++;
        char *name_start = p;
        while (*p && *p != '\n') p++;
        if (*p) *p++ = '\0';
        char *sp = strchr(name_start, ' ');
        if (sp) *sp = '\0';

        char *seq_start = p;
        while (*p && *p != '\n') p++;
        if (*p) *p++ = '\0';

        while (*p && *p != '\n') p++;
        if (*p) p++;

        char *qual_start = p;
        while (*p && *p != '\n') p++;
        if (*p) *p++ = '\0';

        if (n >= capacity) {
            size_t new_cap = capacity * 2;
            names = (char **)realloc(names, new_cap * sizeof(char *));
            seqs  = (char **)realloc(seqs,  new_cap * sizeof(char *));
            quals = (char **)realloc(quals, new_cap * sizeof(char *));
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
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <file|memory> <index> <reads.fq> [reads2.fq] [iterations]\n", argv[0]);
        return 1;
    }

    const char *mode = argv[1];
    const char *index = argv[2];
    const char *reads1_path = argv[3];
    const char *reads2_path = NULL;
    int iterations = 3;

    int argi = 4;
    /* Check if next arg is a file (paired-end) or a number (iterations) */
    if (argi < argc) {
        char *endp;
        long val = strtol(argv[argi], &endp, 10);
        if (*endp == '\0' && val > 0) {
            iterations = (int)val;
        } else {
            reads2_path = argv[argi];
            argi++;
            if (argi < argc) {
                iterations = atoi(argv[argi]);
                if (iterations < 1) iterations = 1;
            }
        }
    }

    int use_memory = (strcmp(mode, "memory") == 0);
    int paired = (reads2_path != NULL);

    /* Pre-load reads if in-memory mode */
    char **names1 = NULL, **seqs1 = NULL, **quals1 = NULL;
    size_t n1 = 0;
    char *backing1 = NULL;
    char **names2 = NULL, **seqs2 = NULL, **quals2 = NULL;
    size_t n2 = 0;
    char *backing2 = NULL;

    if (use_memory) {
        if (read_fastq(reads1_path, &names1, &seqs1, &quals1, &n1, &backing1) != 0)
            return 1;
        if (paired) {
            if (read_fastq(reads2_path, &names2, &seqs2, &quals2, &n2, &backing2) != 0)
                return 1;
        }
    }

    /* Measure context creation */
    double t0 = now_ms();
    bt2_align_config_t config;
    bt2_align_config_init(&config);
    config.index_path = index;
    config.quiet = 1;

    int err;
    bt2_align_ctx_t *ctx = bt2_align_create(&config, &err);
    double create_ms = now_ms() - t0;

    if (!ctx) {
        fprintf(stderr, "bt2_align_create failed: %s\n", bt2_strerror(err));
        return 1;
    }

    /* Run alignment iterations */
    double total_align_ms = 0;
    int64_t total_records = 0;
    int64_t total_reads = 0;

    for (int i = 0; i < iterations; i++) {
        bt2_align_output_t *output = NULL;
        bt2_align_stats_t stats;
        int rc;

        double align_t0 = now_ms();
        if (use_memory) {
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
            rc = bt2_align_run(ctx, &input, &output, &stats);
        } else {
            const char *m1[] = { reads1_path };
            const char *m2[] = { reads2_path };
            rc = bt2_align_run_files(ctx, m1, 1,
                                     paired ? m2 : NULL, paired ? 1 : 0,
                                     &output, &stats);
        }
        double align_ms = now_ms() - align_t0;

        if (rc != BT2_OK) {
            fprintf(stderr, "Alignment failed (iter %d): %s (%s)\n",
                    i, bt2_strerror(rc), bt2_align_last_error(ctx));
            bt2_align_destroy(ctx);
            return 1;
        }

        total_align_ms += align_ms;
        total_records += (int64_t)output->n_records;
        total_reads += stats.n_reads;
        bt2_align_output_free(output);
    }

    bt2_align_destroy(ctx);

    /* Output JSON */
    printf("{\"mode\":\"%s\",\"paired\":%s,\"iterations\":%d,"
           "\"create_ms\":%.2f,"
           "\"total_align_ms\":%.2f,\"avg_align_ms\":%.2f,"
           "\"total_records\":%lld,\"total_reads\":%lld}\n",
           mode, paired ? "true" : "false", iterations,
           create_ms,
           total_align_ms, total_align_ms / iterations,
           (long long)total_records, (long long)total_reads);

    free(names1); free(seqs1); free(quals1); free(backing1);
    free(names2); free(seqs2); free(quals2); free(backing2);
    return 0;
}
