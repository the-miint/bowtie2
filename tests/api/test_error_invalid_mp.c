/*
 * Regression test for GUIDANCE_BOWTIE2_MP_AND_THROW.md.
 *
 * Issue 1 (P0, robustness): an invalid --mp where MAX < MIN must return
 *   BT2_ERR_INVALID_CONFIG with a populated last_error — it must NOT abort
 *   the process. Before the fix, parseOptions() (run by
 *   apply_config_to_statics() outside the driver try/catch) threw past the
 *   extern "C" boundary on the in-memory path and SIGABRT'd the worker.
 *
 * Issue 2 (functionality): with both mismatch_penalty and
 *   mismatch_penalty_min set, the API can express --mp MX,MN (e.g. woltka's
 *   --mp 1,1) and the alignment runs successfully.
 *
 * Linked against bowtie2-align-s-lib (BT2_NO_MAIN); run from
 * PROJECT_SOURCE_DIR, where the lambda_virus index lives.
 *
 * NOTE: uses CHECK() (not assert()) because the library is built Release
 * with NDEBUG, which compiles assert() out entirely.
 */
#include "bt2_api.h"
#include "test_helpers.h"
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #cond); \
        g_failures++; \
    } \
} while (0)

static const char *names[] = { "read1" };
static const char *seqs[]  = {
    "ACGTACGTACGTACGTACGTACGTACGTACGTACGTACGTAC"
};
static const char *quals[] = {
    "IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII"
};

static void fill_input(bt2_input_t *input) {
    bt2_input_init(input);
    input->names   = names;
    input->seqs    = seqs;
    input->quals   = quals;
    input->n_reads = 1;
}

int main(void) {
    const char *index = "example/index/lambda_virus";

    /* ---- Issue 1: invalid --mp (MAX=1 < default MIN=2) must not abort ---- */
    {
        bt2_align_config_t config;
        bt2_align_config_init(&config);
        config.index_path = index;
        config.quiet = 1;
        config.mismatch_penalty = 1;   /* single value < min default -> inverts */

        int err = BT2_OK;
        bt2_align_ctx_t *ctx = bt2_align_create(&config, &err);
        CHECK(ctx != NULL);
        CHECK(err == BT2_OK);

        if (ctx) {
            bt2_input_t input;
            fill_input(&input);

            bt2_align_output_t *output = NULL;
            int rc = bt2_align_run(ctx, &input, &output, NULL);

            printf("invalid --mp: rc=%d (%s) last_error='%s'\n",
                   rc, bt2_strerror(rc), bt2_align_last_error(ctx));

            /* The whole point: a clean error return; process still alive. */
            CHECK(rc == BT2_ERR_INVALID_CONFIG);
            CHECK(strlen(bt2_align_last_error(ctx)) > 0);
            CHECK(output == NULL);

            if (output) bt2_align_output_free(output);
            bt2_align_destroy(ctx);
        }
    }

    /* ---- Issue 2: --mp 1,1 (both set) is expressible and runs ---- */
    {
        bt2_align_config_t config;
        bt2_align_config_init(&config);
        config.index_path = index;
        config.quiet = 1;
        config.mismatch_penalty     = 1;
        config.mismatch_penalty_min = 1;

        int err = BT2_OK;
        bt2_align_ctx_t *ctx = bt2_align_create(&config, &err);
        CHECK(ctx != NULL);
        CHECK(err == BT2_OK);

        if (ctx) {
            bt2_input_t input;
            fill_input(&input);

            bt2_align_output_t *output = NULL;
            int rc = bt2_align_run(ctx, &input, &output, NULL);

            printf("--mp 1,1: rc=%d (%s) last_error='%s' records=%zu\n",
                   rc, bt2_strerror(rc), bt2_align_last_error(ctx),
                   output ? output->n_records : (size_t)0);

            CHECK(rc == BT2_OK);
            CHECK(output != NULL);

            if (output) bt2_align_output_free(output);
            bt2_align_destroy(ctx);
        }
    }

    if (g_failures != 0) {
        printf("test_error_invalid_mp: FAILED (%d check(s))\n", g_failures);
        return 1;
    }
    printf("test_error_invalid_mp: PASSED\n");
    return 0;
}
