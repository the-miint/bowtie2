/*
 * Test that bt2_align_config_init sets all fields to expected defaults
 * and that bt2_align_create validates config properly.
 */

#include "bt2_api.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    bt2_align_config_t config;
    int err;

    /* NULL config_init is a safe no-op */
    bt2_align_config_init(NULL);

    /* Zero-fill with 0xFF to detect any fields config_init misses */
    memset(&config, 0xFF, sizeof(config));

    bt2_align_config_init(&config);

    /* struct_size must equal sizeof the struct */
    assert(config.struct_size == sizeof(bt2_align_config_t));

    /* index_path defaults to NULL (caller must set) */
    assert(config.index_path == NULL);

    /* seed defaults to 0 */
    assert(config.seed == 0);

    /* nthreads defaults to 1 (single-threaded per GPL-boundary guidance) */
    assert(config.nthreads == 1);

    /* preset defaults to SENSITIVE */
    assert(config.preset == BT2_PRESET_SENSITIVE);

    /* local_align defaults to 0 (end-to-end) */
    assert(config.local_align == 0);

    /* quiet defaults to 1 (library should be quiet by default) */
    assert(config.quiet == 1);

    /* log callback defaults to NULL */
    assert(config.log_fn == NULL);
    assert(config.log_user_data == NULL);

    /* --- New config option defaults --- */

    /* Mate orientation enum constants */
    assert(BT2_MATE_FR == 0);
    assert(BT2_MATE_RF == 1);
    assert(BT2_MATE_FF == 2);

    /* Reporting */
    assert(config.k == 0);
    assert(config.report_all == 0);

    /* Trimming */
    assert(config.trim5 == 0);
    assert(config.trim3 == 0);

    /* Scoring — sentinel -1 means "use bowtie2 default" */
    assert(config.match_bonus == -1);
    assert(config.mismatch_penalty == -1);
    assert(config.n_penalty == -1);
    assert(config.read_gap_open == -1);
    assert(config.read_gap_extend == -1);
    assert(config.ref_gap_open == -1);
    assert(config.ref_gap_extend == -1);
    assert(config.score_min == NULL);

    /* Paired-end */
    assert(config.min_insert == -1);
    assert(config.max_insert == -1);
    assert(config.mate_orientation == BT2_MATE_FR);
    assert(config.no_mixed == 0);
    assert(config.no_discordant == 0);
    assert(config.dovetail == 0);
    assert(config.no_contain == 0);
    assert(config.no_overlap == 0);

    /* Strand */
    assert(config.nofw == 0);
    assert(config.norc == 0);

    /* Effort — sentinel -1 means "preset-dependent" */
    assert(config.seed_mismatches == -1);
    assert(config.seed_length == -1);
    assert(config.max_dp_failures == -1);
    assert(config.max_seed_rounds == -1);

    /* SAM output */
    assert(config.no_unal == 0);
    assert(config.xeq == 0);
    assert(config.rg_id == NULL);

    /* Other */
    assert(config.ignore_quals == 0);
    assert(config.reorder == 0);

    /* --- bt2_align_create validation tests --- */

    /* NULL config returns NULL with BT2_ERR_INVALID_CONFIG */
    err = BT2_OK;
    assert(bt2_align_create(NULL, &err) == NULL);
    assert(err == BT2_ERR_INVALID_CONFIG);

    /* Config with no index_path returns NULL */
    err = BT2_OK;
    assert(bt2_align_create(&config, &err) == NULL);
    assert(err == BT2_ERR_INVALID_CONFIG);

    /* Config with wrong struct_size returns NULL */
    config.index_path = "some/index";
    config.struct_size = 1;  /* wrong size */
    err = BT2_OK;
    assert(bt2_align_create(&config, &err) == NULL);
    assert(err == BT2_ERR_INVALID_CONFIG);

    /* k + report_all conflict returns BT2_ERR_INVALID_CONFIG */
    bt2_align_config_init(&config);
    config.index_path = "example/index/lambda_virus";
    config.k = 5;
    config.report_all = 1;
    err = BT2_OK;
    assert(bt2_align_create(&config, &err) == NULL);
    assert(err == BT2_ERR_INVALID_CONFIG);

    /* NULL error_out is safe (doesn't crash) */
    bt2_align_config_init(&config);
    config.index_path = NULL;
    assert(bt2_align_create(&config, NULL) == NULL);

    printf("test_config_init: PASSED\n");
    return 0;
}
