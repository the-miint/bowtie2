/*
 * bt2_align_create and bt2_build_create check nthreads:
 *   - below 1 is refused with BT2_ERR_INVALID_CONFIG (the builder would
 *     otherwise dereference NULL when it runs);
 *   - above 1 is refused the same way in a WebAssembly build without
 *     pthreads, which cannot start a thread, and accepted elsewhere;
 *   - 1 is accepted everywhere.
 * A refusal is logged through log_fn, naming nthreads.
 *
 * Only the contexts are created; nothing is aligned or built. Explicit
 * checks rather than assert(), so the test checks in any build. Run from
 * PROJECT_SOURCE_DIR, where the example index and reference live.
 */
#include "bt2_api.h"
#include "test_helpers.h"
#include <stdio.h>
#include <string.h>

#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
#define THREADS_AVAILABLE 0
#else
#define THREADS_AVAILABLE 1
#endif

#define INDEX "example/index/lambda_virus"
#define REF   "example/reference/lambda_virus.fa"

static int g_failures = 0;

/* Set when log_fn gets an error naming nthreads. */
static int g_logged_nthreads;

static void log_fn(void *user_data, int level, const char *msg) {
    (void)user_data;
    if (level == BT2_LOG_ERROR && strstr(msg, "nthreads") != NULL) {
        g_logged_nthreads = 1;
    }
}

/* Returns the error code; BT2_OK if the context was created. */
static int try_align(int nthreads) {
    bt2_align_config_t config;
    bt2_align_config_init(&config);
    config.index_path = INDEX;
    config.nthreads = nthreads;
    config.log_fn = log_fn;
    int err = BT2_OK;
    bt2_align_ctx_t *ctx = bt2_align_create(&config, &err);
    if (ctx == NULL) return err == BT2_OK ? BT2_ERR_INTERNAL : err;
    bt2_align_destroy(ctx);
    return BT2_OK;
}

static int try_build(int nthreads) {
    const char *refs[] = { REF };
    bt2_build_config_t config;
    bt2_build_config_init(&config);
    config.ref_paths = refs;
    config.n_ref_paths = 1;
    config.output_base = "tests/api/data/never_built";
    config.nthreads = nthreads;
    config.log_fn = log_fn;
    int err = BT2_OK;
    bt2_build_ctx_t *ctx = bt2_build_create(&config, &err);
    if (ctx == NULL) return err == BT2_OK ? BT2_ERR_INTERNAL : err;
    bt2_build_destroy(ctx);
    return BT2_OK;
}

static void expect(const char *what, int nthreads, int (*try_create)(int),
                   int want_ok) {
    g_logged_nthreads = 0;
    int rc = try_create(nthreads);
    if (want_ok) {
        CHECK_MSG(rc == BT2_OK, "%s refused nthreads=%d: %s", what, nthreads,
                  bt2_strerror(rc));
    } else {
        CHECK_MSG(rc == BT2_ERR_INVALID_CONFIG, "%s with nthreads=%d returned %s, "
                  "not BT2_ERR_INVALID_CONFIG", what, nthreads, bt2_strerror(rc));
        CHECK_MSG(g_logged_nthreads, "%s refused nthreads=%d without logging a "
                  "message naming nthreads", what, nthreads);
    }
}

int main(void) {
    const struct { const char *what; int (*try_create)(int); } apis[] = {
        { "bt2_align_create", try_align },
        { "bt2_build_create", try_build },
    };
    for (size_t i = 0; i < sizeof(apis) / sizeof(apis[0]); i++) {
        expect(apis[i].what, 0, apis[i].try_create, 0);
        expect(apis[i].what, -1, apis[i].try_create, 0);
        expect(apis[i].what, 1, apis[i].try_create, 1);
        expect(apis[i].what, 2, apis[i].try_create, THREADS_AVAILABLE);
    }
    if (g_failures != 0) {
        fprintf(stderr, "test_nthreads_config: %d check(s) failed\n", g_failures);
        return 1;
    }
    printf("test_nthreads_config: PASSED (threads %savailable)\n",
           THREADS_AVAILABLE ? "" : "not ");
    return 0;
}
