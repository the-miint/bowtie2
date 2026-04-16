/*
 * Shared argv builder for bt2_align_config_t.
 * Used by both bt2_api.cpp (file-based path) and bt2_search.cpp (memory path).
 * Not part of the public C API — internal only.
 */

#ifndef BT2_CONFIG_ARGV_H
#define BT2_CONFIG_ARGV_H

#include "bt2_api.h"
#include "scoring.h"
#include <vector>
#include <cstdio>
#include <cinttypes>

/*
 * Scratch buffers for snprintf'd numeric values.
 * Must outlive the argv vector that references them.
 */
struct ConfigArgvBufs {
	char k[32], trim5[32], trim3[32];
	char ma[32], mp[32], np[32], rdg[64], rfg[64];
	char minins[32], maxins[32];
	char seedmm[32], seedlen[32], dp[32], rounds[32];
};

/*
 * Append alignment option flags from config to argv.
 * Does NOT add bowtie2, -x, preset, -p, --seed, --quiet, or input/output
 * args — those are caller-specific. Only adds option flags when
 * non-sentinel.
 *
 * bufs must point to a ConfigArgvBufs whose lifetime covers all
 * subsequent use of argv (snprintf'd strings reference bufs).
 */
inline void append_config_argv(const bt2_align_config_t *config,
                               std::vector<const char *> &argv,
                               ConfigArgvBufs *bufs) {
	/* Reporting */
	if (config->k > 0) {
		snprintf(bufs->k, sizeof(bufs->k), "%d", config->k);
		argv.push_back("-k");
		argv.push_back(bufs->k);
	}
	if (config->report_all) {
		argv.push_back("-a");
	}

	/* Trimming */
	if (config->trim5 > 0) {
		snprintf(bufs->trim5, sizeof(bufs->trim5), "%d", config->trim5);
		argv.push_back("--trim5");
		argv.push_back(bufs->trim5);
	}
	if (config->trim3 > 0) {
		snprintf(bufs->trim3, sizeof(bufs->trim3), "%d", config->trim3);
		argv.push_back("--trim3");
		argv.push_back(bufs->trim3);
	}

	/* Scoring */
	if (config->match_bonus >= 0) {
		snprintf(bufs->ma, sizeof(bufs->ma), "%d", config->match_bonus);
		argv.push_back("--ma");
		argv.push_back(bufs->ma);
	}
	if (config->mismatch_penalty >= 0) {
		snprintf(bufs->mp, sizeof(bufs->mp), "%d", config->mismatch_penalty);
		argv.push_back("--mp");
		argv.push_back(bufs->mp);
	}
	if (config->n_penalty >= 0) {
		snprintf(bufs->np, sizeof(bufs->np), "%d", config->n_penalty);
		argv.push_back("--np");
		argv.push_back(bufs->np);
	}
	if (config->read_gap_open >= 0 || config->read_gap_extend >= 0) {
		int rgo = config->read_gap_open >= 0 ? config->read_gap_open : DEFAULT_READ_GAP_CONST;
		int rge = config->read_gap_extend >= 0 ? config->read_gap_extend : DEFAULT_READ_GAP_LINEAR;
		snprintf(bufs->rdg, sizeof(bufs->rdg), "%d,%d", rgo, rge);
		argv.push_back("--rdg");
		argv.push_back(bufs->rdg);
	}
	if (config->ref_gap_open >= 0 || config->ref_gap_extend >= 0) {
		int fgo = config->ref_gap_open >= 0 ? config->ref_gap_open : DEFAULT_REF_GAP_CONST;
		int fge = config->ref_gap_extend >= 0 ? config->ref_gap_extend : DEFAULT_REF_GAP_LINEAR;
		snprintf(bufs->rfg, sizeof(bufs->rfg), "%d,%d", fgo, fge);
		argv.push_back("--rfg");
		argv.push_back(bufs->rfg);
	}
	if (config->score_min != NULL) {
		argv.push_back("--score-min");
		argv.push_back(config->score_min);
	}

	/* Paired-end */
	if (config->min_insert >= 0) {
		snprintf(bufs->minins, sizeof(bufs->minins), "%d", config->min_insert);
		argv.push_back("-I");
		argv.push_back(bufs->minins);
	}
	if (config->max_insert >= 0) {
		snprintf(bufs->maxins, sizeof(bufs->maxins), "%d", config->max_insert);
		argv.push_back("-X");
		argv.push_back(bufs->maxins);
	}
	switch (config->mate_orientation) {
		case BT2_MATE_RF: argv.push_back("--rf"); break;
		case BT2_MATE_FF: argv.push_back("--ff"); break;
		/* BT2_MATE_FR (0) is bowtie2's default — omit flag */
	}
	if (config->no_mixed)      argv.push_back("--no-mixed");
	if (config->no_discordant) argv.push_back("--no-discordant");
	if (config->dovetail)      argv.push_back("--dovetail");
	if (config->no_contain)    argv.push_back("--no-contain");
	if (config->no_overlap)    argv.push_back("--no-overlap");

	/* Strand */
	if (config->nofw) argv.push_back("--nofw");
	if (config->norc) argv.push_back("--norc");

	/* Effort */
	if (config->seed_mismatches >= 0) {
		snprintf(bufs->seedmm, sizeof(bufs->seedmm), "%d", config->seed_mismatches);
		argv.push_back("-N");
		argv.push_back(bufs->seedmm);
	}
	if (config->seed_length >= 0) {
		snprintf(bufs->seedlen, sizeof(bufs->seedlen), "%d", config->seed_length);
		argv.push_back("-L");
		argv.push_back(bufs->seedlen);
	}
	if (config->max_dp_failures >= 0) {
		snprintf(bufs->dp, sizeof(bufs->dp), "%d", config->max_dp_failures);
		argv.push_back("-D");
		argv.push_back(bufs->dp);
	}
	if (config->max_seed_rounds >= 0) {
		snprintf(bufs->rounds, sizeof(bufs->rounds), "%d", config->max_seed_rounds);
		argv.push_back("-R");
		argv.push_back(bufs->rounds);
	}

	/* SAM output */
	if (config->no_unal) argv.push_back("--no-unal");
	if (config->xeq)     argv.push_back("--xeq");
	if (config->rg_id != NULL) {
		argv.push_back("--rg-id");
		argv.push_back(config->rg_id);
	}

	/* Other */
	if (config->ignore_quals) argv.push_back("--ignore-quals");
	if (config->reorder)      argv.push_back("--reorder");
}

#endif /* BT2_CONFIG_ARGV_H */
