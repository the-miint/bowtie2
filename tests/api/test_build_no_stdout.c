/*
 * Phase 7b test: verify bt2_build_run() does not write to stdout.
 * An embeddable library must not leak data over the host's stdout.
 *
 * Approach: dup stdout to a pipe, run bt2_build_run, read the pipe,
 * verify nothing was written.
 */
#include "bt2_api.h"
#include "test_helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int main(void) {
    const char *base = "/tmp/bt2_test_no_stdout";
    cleanup_index(base);

    /* Set up: create a pipe, redirect stdout to it */
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        perror("pipe");
        return 1;
    }

    /* Make read end non-blocking so we can check without hanging */
    if (fcntl(pipefd[0], F_SETFL, O_NONBLOCK) == -1) {
        perror("fcntl");
        close(pipefd[0]); close(pipefd[1]);
        return 1;
    }

    int orig_stdout = dup(STDOUT_FILENO);
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[1]); /* close write end — stdout fd is the only writer now */

    /* Run the build */
    bt2_build_config_t config;
    bt2_build_config_init(&config);
    const char *refs[] = { "example/reference/lambda_virus.fa" };
    config.ref_paths = refs;
    config.n_ref_paths = 1;
    config.output_base = base;
    config.quiet = 1;

    int err;
    bt2_build_ctx_t *ctx = bt2_build_create(&config, &err);
    if (!ctx) {
        /* Restore stdout before printing error */
        dup2(orig_stdout, STDOUT_FILENO);
        close(orig_stdout);
        close(pipefd[0]);
        fprintf(stderr, "FAIL: bt2_build_create failed\n");
        return 1;
    }

    int rc = bt2_build_run(ctx, NULL);

    /* Restore stdout */
    fflush(stdout);
    dup2(orig_stdout, STDOUT_FILENO);
    close(orig_stdout);

    bt2_build_destroy(ctx);
    cleanup_index(base);

    if (rc != BT2_OK) {
        close(pipefd[0]);
        fprintf(stderr, "FAIL: bt2_build_run returned %d\n", rc);
        return 1;
    }

    /* Check: read from pipe — should be empty */
    char buf[4096];
    ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
    close(pipefd[0]);

    if (n > 0) {
        buf[n] = '\0';
        fprintf(stderr, "FAIL: %zd bytes leaked to stdout:\n%s\n", n, buf);
        return 1;
    }

    printf("test_build_no_stdout: PASSED (no stdout leakage)\n");
    return 0;
}
