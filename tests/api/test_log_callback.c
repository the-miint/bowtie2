/*
 * Phase 5b test: verify the log callback receives messages during
 * alignment. Tests:
 * 1. quiet=0, log_fn set -> callback receives alignment summary messages
 * 2. quiet=1, log_fn set -> callback still receives messages
 * 3. After alignment with callback, output is correct
 */
#include "bt2_api.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define MAX_LOG_MSGS 256
#define MAX_MSG_LEN  1024

static int log_count = 0;
static char log_messages[MAX_LOG_MSGS][MAX_MSG_LEN];
static int log_levels[MAX_LOG_MSGS];

static void test_log_fn(void *user_data, int level, const char *msg) {
    (void)user_data;
    if (log_count < MAX_LOG_MSGS) {
        log_levels[log_count] = level;
        strncpy(log_messages[log_count], msg, MAX_MSG_LEN - 1);
        log_messages[log_count][MAX_MSG_LEN - 1] = '\0';
        log_count++;
    }
}

static void reset_log(void) {
    log_count = 0;
}

static const char *names[] = { "log_test_read" };
static const char *seqs[]  = { "AGCTTTTCATTCTGACTGCAACGGGCAATATGTCTCTGTGT" };

int main(void) {
    /* Test 1: quiet=0, log_fn set — callback should receive messages
       (alignment summary, timing, etc.) */
    {
        reset_log();

        bt2_align_config_t config;
        bt2_align_config_init(&config);
        config.index_path = "example/index/lambda_virus";
        config.quiet = 0;  /* non-quiet: produces alignment summary */
        config.log_fn = test_log_fn;
        config.log_user_data = NULL;

        int err;
        bt2_align_ctx_t *ctx = bt2_align_create(&config, &err);
        assert(ctx != NULL);

        bt2_input_t input;
        bt2_input_init(&input);
        input.names = names;
        input.seqs = seqs;
        input.quals = NULL;
        input.n_reads = 1;

        bt2_align_output_t *output = NULL;
        int rc = bt2_align_run(ctx, &input, &output, NULL);
        assert(rc == BT2_OK);
        assert(output != NULL);
        assert(output->n_records == 1);

        printf("test_log_callback[quiet=0]: received %d log messages\n", log_count);
        if (log_count == 0) {
            fprintf(stderr, "FAIL: expected log messages with quiet=0 and log_fn set\n");
            return 1;
        }

        bt2_align_output_free(output);
        bt2_align_destroy(ctx);
    }

    /* Test 2: quiet=1, log_fn set — callback should still receive messages
       (the callback overrides quiet for delivery) */
    {
        reset_log();

        bt2_align_config_t config;
        bt2_align_config_init(&config);
        config.index_path = "example/index/lambda_virus";
        config.quiet = 1;
        config.log_fn = test_log_fn;
        config.log_user_data = NULL;

        int err;
        bt2_align_ctx_t *ctx = bt2_align_create(&config, &err);
        assert(ctx != NULL);

        bt2_input_t input;
        bt2_input_init(&input);
        input.names = names;
        input.seqs = seqs;
        input.quals = NULL;
        input.n_reads = 1;

        bt2_align_output_t *output = NULL;
        int rc = bt2_align_run(ctx, &input, &output, NULL);
        assert(rc == BT2_OK);
        assert(output != NULL);

        printf("test_log_callback[quiet=1]: received %d log messages\n", log_count);
        /* When quiet=1 and log_fn is set, effective_quiet is forced to 0
           so the alignment summary is produced and delivered to the callback.
           The header doc says: "log_fn receives all log messages regardless
           of quiet setting." */
        if (log_count == 0) {
            fprintf(stderr, "FAIL: expected log messages with quiet=1 and log_fn set\n");
            return 1;
        }

        bt2_align_output_free(output);
        bt2_align_destroy(ctx);
    }

    printf("test_log_callback: PASSED\n");
    return 0;
}
