/*
 * At nthreads=1 the API aligns on the calling thread: it starts no worker,
 * read-ahead or output-writer thread. A WebAssembly build without pthreads
 * cannot start a thread, so this is what lets it align at all.
 *
 * The test sees where the worker runs through log_fn. A 1-base read makes
 * the worker log "Warning: skipping read ...", and the API calls log_fn on
 * the thread that wrote the line. At one thread the test checks that the
 * warning arrives, that every log_fn call runs on the thread that called the
 * API, and, on Linux, that the process has only that one thread while the
 * worker logs. At two threads (where the build has threads) it checks the
 * opposite: the warning comes from a thread of the library's. Both entry
 * points are checked: bt2_align_run (reads in memory) and bt2_align_run_files
 * (tests/api/data/inline_reads.fq, the same two reads).
 *
 * Run from PROJECT_SOURCE_DIR, where the example index lives.
 */
#include "bt2_api.h"
#include "test_helpers.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define INDEX    "example/index/lambda_virus"
#define READS_FQ "tests/api/data/inline_reads.fq"

/* lambda_1001 is 41 bases of example/reference/lambda_virus.fa at position
   1001, so it aligns; one_base is too short for the worker to align. */
static const char *NAMES[] = { "lambda_1001", "one_base" };
static const char *SEQS[]  = { "GCAGCGCAACACCCTTATCTGGTTGCCGACGGATGGTGATG", "A" };
static const char *QUALS[] = { "IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII", "I" };

static int g_failures = 0;

typedef struct {
    pthread_t caller;
    int n_calls;
    int n_off_thread;      /* log_fn calls on a thread other than the caller's */
    int saw_skip_warning;
    int warning_off_thread;
    int tasks_at_warning;  /* threads in the process then; -1 if not counted */
} log_probe_t;

static void probe_log(void *user_data, int level, const char *msg) {
    log_probe_t *p = (log_probe_t *)user_data;
    int off_thread = !pthread_equal(pthread_self(), p->caller);
    p->n_calls++;
    if (off_thread) p->n_off_thread++;
    if (level == BT2_LOG_WARN && strstr(msg, "skipping read 'one_base'") != NULL) {
        p->saw_skip_warning = 1;
        p->warning_off_thread = off_thread;
        p->tasks_at_warning = count_threads();
    }
}

static void check(int nthreads, int in_memory) {
    const char *path = in_memory ? "memory" : "files";
    log_probe_t p;
    memset(&p, 0, sizeof(p));
    p.caller = pthread_self();
    p.tasks_at_warning = -1;

    bt2_align_config_t config;
    bt2_align_config_init(&config);
    config.index_path = INDEX;
    config.nthreads = nthreads;
    config.log_fn = probe_log;
    config.log_user_data = &p;

    int err = BT2_OK;
    bt2_align_ctx_t *ctx = bt2_align_create(&config, &err);
    if (ctx == NULL) {
        CHECK_MSG(0, "[%s, %d thread(s)] bt2_align_create: %s", path, nthreads,
                  bt2_strerror(err));
        return;
    }
    bt2_align_output_t *out = NULL;
    int rc;
    if (in_memory) {
        bt2_input_t input;
        bt2_input_init(&input);
        input.names = NAMES;
        input.seqs = SEQS;
        input.quals = QUALS;
        input.n_reads = 2;
        rc = bt2_align_run(ctx, &input, &out, NULL);
    } else {
        const char *inputs[] = { READS_FQ };
        rc = bt2_align_run_files(ctx, inputs, 1, NULL, 0, &out, NULL);
    }

    CHECK_MSG(rc == BT2_OK, "[%s, %d thread(s)] run returned %s: %s", path,
              nthreads, bt2_strerror(rc), bt2_align_last_error(ctx));
    CHECK_MSG(aligned_by_name(out, "lambda_1001"),
              "[%s, %d thread(s)] lambda_1001 did not align", path, nthreads);
    CHECK_MSG(p.saw_skip_warning, "[%s, %d thread(s)] log_fn never got the "
              "worker's skip warning (%d calls)", path, nthreads, p.n_calls);
    if (nthreads == 1) {
        CHECK_MSG(p.n_off_thread == 0, "[%s, 1 thread] %d of %d log_fn calls "
                  "ran on a thread other than the caller's", path,
                  p.n_off_thread, p.n_calls);
        CHECK_MSG(p.tasks_at_warning <= 1, "[%s, 1 thread] %d threads were "
                  "alive while the worker logged; expected 1", path,
                  p.tasks_at_warning);
    } else {
        CHECK_MSG(!p.saw_skip_warning || p.warning_off_thread,
                  "[%s, %d threads] the worker's warning came from the "
                  "caller's thread: no worker thread ran", path, nthreads);
    }
    printf("test_align_inline[%s, %d thread(s)]: %d log_fn calls, %d off the "
           "caller's thread; threads while the worker logged: %d\n", path,
           nthreads, p.n_calls, p.n_off_thread, p.tasks_at_warning);

    bt2_align_output_free(out);
    bt2_align_destroy(ctx);
}

int main(void) {
    int max_threads = TEST_HAS_THREADS ? 2 : 1;
    for (int nthreads = 1; nthreads <= max_threads; nthreads++) {
        check(nthreads, 1);
        check(nthreads, 0);
    }
    if (g_failures != 0) {
        fprintf(stderr, "test_align_inline: %d check(s) failed\n", g_failures);
        return 1;
    }
    printf("test_align_inline: PASSED\n");
    return 0;
}
