/*
 * At nthreads=1 the API aligns on the calling thread: it starts no worker,
 * read-ahead or output-writer thread. A WebAssembly build without pthreads
 * cannot start a thread, so this is what lets it align at all.
 *
 * The test sees where the worker runs through log_fn. A 1-base read makes
 * the worker log "Warning: skipping read ...", and the API calls log_fn on
 * the thread that wrote the line. The test checks that the warning arrives,
 * that every log_fn call runs on the thread that called the API, and, on
 * Linux, that the process has only that one thread while the worker logs.
 * Both entry points are checked: bt2_align_run (reads in memory) and
 * bt2_align_run_files (tests/api/data/inline_reads.fq, the same two reads).
 *
 * Explicit checks rather than assert(), so the test checks in any build.
 * Run from PROJECT_SOURCE_DIR, where the example index lives.
 */
#include "bt2_api.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <dirent.h>
#define COUNT_TASKS 1
#endif

#define INDEX    "example/index/lambda_virus"
#define READS_FQ "tests/api/data/inline_reads.fq"

/* lambda_1001 is 41 bases of example/reference/lambda_virus.fa at position
   1001, so it aligns; one_base is too short for the worker to align. */
static const char *NAMES[] = { "lambda_1001", "one_base" };
static const char *SEQS[]  = { "GCAGCGCAACACCCTTATCTGGTTGCCGACGGATGGTGATG", "A" };
static const char *QUALS[] = { "IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII", "I" };

#define SAM_FLAG_UNMAPPED 0x4

typedef struct {
    pthread_t caller;
    int n_calls;
    int n_off_thread;      /* log_fn calls on a thread other than the caller's */
    int saw_skip_warning;
    int tasks_at_warning;  /* threads in the process then; -1 if not counted */
} log_probe_t;

static int count_tasks(void) {
#ifdef COUNT_TASKS
    DIR *d = opendir("/proc/self/task");
    if (d == NULL) return -1;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] != '.') n++;
    }
    closedir(d);
    return n;
#else
    return -1;
#endif
}

static void probe_log(void *user_data, int level, const char *msg) {
    log_probe_t *p = (log_probe_t *)user_data;
    p->n_calls++;
    if (!pthread_equal(pthread_self(), p->caller)) p->n_off_thread++;
    if (level == BT2_LOG_WARN && strstr(msg, "skipping read 'one_base'") != NULL) {
        p->saw_skip_warning = 1;
        p->tasks_at_warning = count_tasks();
    }
}

static void probe_init(log_probe_t *p) {
    memset(p, 0, sizeof(*p));
    p->caller = pthread_self();
    p->tasks_at_warning = -1;
}

static bt2_align_ctx_t *make_ctx(const char *label, log_probe_t *probe) {
    bt2_align_config_t config;
    bt2_align_config_init(&config);
    config.index_path = INDEX;
    config.nthreads = 1;
    config.log_fn = probe_log;
    config.log_user_data = probe;

    int err = BT2_OK;
    bt2_align_ctx_t *ctx = bt2_align_create(&config, &err);
    if (ctx == NULL) {
        fprintf(stderr, "FAIL[%s]: bt2_align_create: %s\n", label, bt2_strerror(err));
    }
    return ctx;
}

/* Returns the number of failed checks. */
static int check(const char *label, int rc, bt2_align_ctx_t *ctx,
                 const log_probe_t *p, const bt2_align_output_t *out) {
    int failures = 0;
    if (rc != BT2_OK) {
        fprintf(stderr, "FAIL[%s]: run returned %s: %s\n", label, bt2_strerror(rc),
                bt2_align_last_error(ctx));
        return 1;
    }
    if (!p->saw_skip_warning) {
        fprintf(stderr, "FAIL[%s]: log_fn never got the worker's skip warning "
                        "(%d calls)\n", label, p->n_calls);
        failures++;
    }
    if (p->n_off_thread != 0) {
        fprintf(stderr, "FAIL[%s]: %d of %d log_fn calls ran on a thread other "
                        "than the caller's\n", label, p->n_off_thread, p->n_calls);
        failures++;
    }
    if (p->tasks_at_warning > 1) {
        fprintf(stderr, "FAIL[%s]: %d threads were alive while the worker "
                        "logged; expected 1\n", label, p->tasks_at_warning);
        failures++;
    }
    int aligned = 0;
    for (size_t i = 0; out != NULL && i < out->n_records; i++) {
        if (strcmp(out->qname[i], "lambda_1001") == 0 &&
            !(out->flag[i] & SAM_FLAG_UNMAPPED)) {
            aligned = 1;
        }
    }
    if (!aligned) {
        fprintf(stderr, "FAIL[%s]: lambda_1001 did not align\n", label);
        failures++;
    }
    if (failures == 0) {
        printf("test_align_inline[%s]: %d log_fn calls, all on the caller's thread; "
               "threads while the worker logged: %d\n",
               label, p->n_calls, p->tasks_at_warning);
    }
    return failures;
}

static int run_memory(void) {
    log_probe_t probe;
    probe_init(&probe);
    bt2_align_ctx_t *ctx = make_ctx("memory", &probe);
    if (ctx == NULL) return 1;

    bt2_input_t input;
    bt2_input_init(&input);
    input.names = NAMES;
    input.seqs = SEQS;
    input.quals = QUALS;
    input.n_reads = 2;

    bt2_align_output_t *out = NULL;
    int rc = bt2_align_run(ctx, &input, &out, NULL);
    int failures = check("memory", rc, ctx, &probe, out);
    bt2_align_output_free(out);
    bt2_align_destroy(ctx);
    return failures;
}

static int run_files(void) {
    log_probe_t probe;
    probe_init(&probe);
    bt2_align_ctx_t *ctx = make_ctx("files", &probe);
    if (ctx == NULL) return 1;

    const char *inputs[] = { READS_FQ };
    bt2_align_output_t *out = NULL;
    int rc = bt2_align_run_files(ctx, inputs, 1, NULL, 0, &out, NULL);
    int failures = check("files", rc, ctx, &probe, out);
    bt2_align_output_free(out);
    bt2_align_destroy(ctx);
    return failures;
}

int main(void) {
    int failures = run_memory() + run_files();
    if (failures != 0) {
        fprintf(stderr, "test_align_inline: %d check(s) failed\n", failures);
        return 1;
    }
    printf("test_align_inline: PASSED\n");
    return 0;
}
