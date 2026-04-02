/*
 * Copyright 2026, bowtie2 contributors
 *
 * This file is part of Bowtie 2.
 *
 * Bowtie 2 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

/**
 * @file aln_sink_columnar.h
 * @brief AlnSink subclass that accumulates alignment results into SOA
 *        (Structure of Arrays) columns for the reentrant C API.
 *
 * Instead of formatting SAM text, this sink extracts structured fields
 * from AlnRes/AlnFlags/AlnSetSumm and stores them in per-thread vectors.
 * After alignment completes, finalize() packs everything into a single
 * bt2_align_output_t allocation.
 */

#ifndef ALN_SINK_COLUMNAR_H
#define ALN_SINK_COLUMNAR_H

#include "aln_sink.h"
#include "bt2_api.h"
#include "threading.h"
#include <vector>
#include <string>

/**
 * Per-thread accumulation buffer for columnar alignment results.
 * Each worker thread appends to its own buffer (no locking during alignment).
 */
struct ColumnarThreadBuf {
	std::vector<std::string> qnames;
	std::vector<int32_t>     flags;
	std::vector<std::string> rnames;
	std::vector<int64_t>     positions;
	std::vector<uint8_t>     mapqs;
	std::vector<std::string> cigars;
	std::vector<std::string> rnexts;
	std::vector<int64_t>     pnexts;
	std::vector<int64_t>     tlens;
	std::vector<std::string> seqs;
	std::vector<std::string> quals;
	/* Optional tags */
	std::vector<int32_t>     tag_as;
	std::vector<int32_t>     tag_xs;
	std::vector<int32_t>     tag_nm;
	std::vector<std::string> tag_md;
	std::vector<std::string> tag_yt;
	/* Read IDs for ordering */
	std::vector<uint64_t>    rdids;

	void clear() {
		qnames.clear(); flags.clear(); rnames.clear();
		positions.clear(); mapqs.clear(); cigars.clear();
		rnexts.clear(); pnexts.clear(); tlens.clear();
		seqs.clear(); quals.clear();
		tag_as.clear(); tag_xs.clear(); tag_nm.clear();
		tag_md.clear(); tag_yt.clear(); rdids.clear();
	}

	size_t size() const { return qnames.size(); }
};

class AlnSinkColumnar : public AlnSink {

	typedef EList<std::string> StrList;

public:

	AlnSinkColumnar(
		OutputQueue& oq,
		const StrList& refnames,
		bool quiet,
		size_t nthreads);

	virtual ~AlnSinkColumnar() { }

	/**
	 * Override: accumulate alignment data into per-thread columnar buffers
	 * instead of formatting SAM text.
	 */
	virtual void append(
		BTString&     o,
		StackedAln&   staln,
		size_t        threadId,
		const Read*   rd1,
		const Read*   rd2,
		const TReadId rdid,
		AlnRes*       rs1,
		AlnRes*       rs2,
		const AlnSetSumm& summ,
		const SeedAlSumm& ssm1,
		const SeedAlSumm& ssm2,
		const AlnFlags*   flags1,
		const AlnFlags*   flags2,
		const PerReadMetrics& prm,
		const Mapq&       mapq,
		const Scoring&    sc,
		bool              report2);

	/**
	 * Pack all accumulated per-thread data into a single-allocation
	 * bt2_align_output_t. Records are sorted by read ID to match
	 * the native bowtie2 output order.
	 *
	 * Returns NULL on allocation failure.
	 * Caller must free with bt2_align_output_free().
	 */
	bt2_align_output_t *finalize();

	/** Total records accumulated across all threads. */
	size_t numRecords() const;

private:

	void appendMate(
		StackedAln&       staln,
		size_t            threadId,
		const Read&       rd,
		const Read*       rdo,
		const TReadId     rdid,
		AlnRes*           rs,
		AlnRes*           rso,
		const AlnSetSumm& summ,
		const AlnFlags&   flags,
		const Mapq&       mapqCalc,
		const Scoring&    sc);

	std::vector<ColumnarThreadBuf> thread_bufs_;
};

#endif /* ALN_SINK_COLUMNAR_H */
