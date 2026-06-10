/*
 * Regression test for the --mm (memory_mapped) index-mapping leak.
 *
 * Before the munmap-on-teardown fix, each bt2_align_run_files() reconstructed
 * the index with useMm=true (mmap'ing the .1/.2/.4 files) but never released
 * the mappings, so the count of index-file VMAs in /proc/self/maps grew
 * linearly per batch (~2-3 per run) toward vm.max_map_count exhaustion. The
 * fix munmaps in ~Ebwt()/~BitPairReference(), so each run maps then unmaps and
 * the live index-VMA count between runs stays flat.
 *
 * This is the only test that exercises many runs in ONE long-lived process,
 * which is the condition that surfaces the leak (the rest run one alignment
 * per process, where process exit masks it).
 *
 * Linux-only: it reads /proc/self/maps. On platforms without it the test
 * skips (passes).
 *
 * NOTE: API tests are built with NDEBUG, so assert() is a no-op. Use explicit
 * checks that return non-zero on failure.
 */
#include "bt2_api.h"
#include <stdio.h>
#include <string.h>

#define INDEX_BASENAME "lambda_virus"
#define N_RUNS 15

/* Count VMAs in /proc/self/maps whose pathname mentions the index basename.
   Returns -1 if /proc/self/maps is unavailable (non-Linux). */
static int count_index_vmas(void) {
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return -1;
    char line[4096];
    int count = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, INDEX_BASENAME) != NULL) count++;
    }
    fclose(f);
    return count;
}

int main(void) {
    if (count_index_vmas() < 0) {
        printf("test_mm_no_vma_leak: SKIP (no /proc/self/maps; non-Linux)\n");
        return 0;
    }

    bt2_align_config_t config;
    bt2_align_config_init(&config);
    config.index_path = "example/index/lambda_virus";
    config.quiet = 1;
    config.memory_mapped = 1;   /* the path under test */

    int err;
    bt2_align_ctx_t *ctx = bt2_align_create(&config, &err);
    if (ctx == NULL) {
        fprintf(stderr, "test_mm_no_vma_leak: bt2_align_create failed: %s\n",
                bt2_strerror(err));
        return 1;
    }

    const char *inputs[] = {"example/reads/longreads.fq"};
    int first_count = -1, last_count = -1, max_count = -1;

    for (int i = 0; i < N_RUNS; i++) {
        bt2_align_output_t *out = NULL;
        int rc = bt2_align_run_files(ctx, inputs, 1, NULL, 0, &out, NULL);
        if (rc != BT2_OK) {
            fprintf(stderr, "test_mm_no_vma_leak: run %d failed: %s\n",
                    i, bt2_align_last_error(ctx));
            bt2_align_destroy(ctx);
            return 1;
        }
        if (out == NULL || out->n_records == 0) {
            fprintf(stderr, "test_mm_no_vma_leak: run %d produced no records\n", i);
            bt2_align_output_free(out);
            bt2_align_destroy(ctx);
            return 1;
        }
        bt2_align_output_free(out);

        /* Sampled AFTER the run returns: the run's index objects have been
           destroyed, so a correct teardown leaves zero live index mappings. */
        int c = count_index_vmas();
        if (i == 0) first_count = c;
        last_count = c;
        if (c > max_count) max_count = c;
    }

    bt2_align_destroy(ctx);

    printf("test_mm_no_vma_leak: index VMAs after run 1 = %d, after run %d = %d, max = %d\n",
           first_count, N_RUNS, last_count, max_count);

    /* A leak shows monotonic growth (~2-3 VMAs/run, ~30-45 over 15 runs).
       With the fix, each run fully unmaps, so the count must not grow beyond
       the first post-run sample. */
    if (last_count > first_count || max_count > first_count) {
        fprintf(stderr, "test_mm_no_vma_leak: FAIL -- index VMA count grew "
                "(first=%d max=%d last=%d): index mappings leaked across runs\n",
                first_count, max_count, last_count);
        return 1;
    }

    printf("test_mm_no_vma_leak: PASSED (no index VMA growth across %d runs)\n", N_RUNS);
    return 0;
}
