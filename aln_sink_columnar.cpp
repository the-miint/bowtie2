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

#include "aln_sink_columnar.h"
#include "aligner_result.h"
#include "read.h"
#include "edit.h"
#include "sam.h"
#include <algorithm>
#include <cctype>
#include <climits>
#include <cstring>
#include <cstdlib>

/* Truncate reference name at first whitespace (matches SamConfig::printRefName) */
static std::string truncate_refname(const std::string& name) {
	size_t end = 0;
	while(end < name.size() && !isspace((unsigned char)name[end])) {
		end++;
	}
	return name.substr(0, end);
}

AlnSinkColumnar::AlnSinkColumnar(
	OutputQueue& oq,
	const StrList& refnames,
	bool quiet,
	size_t nthreads)
	: AlnSink(oq, refnames, quiet),
	  thread_bufs_(nthreads)
{ }

void AlnSinkColumnar::append(
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
	bool              report2)
{
	(void)o;
	(void)ssm1;
	(void)ssm2;
	(void)prm;

	if(rd1 != NULL) {
		assert(flags1 != NULL);
		appendMate(staln, threadId, *rd1, rd2, rdid,
		           rs1, rs2, summ, *flags1, mapq, sc);
	}
	if(rd2 != NULL && report2) {
		assert(flags2 != NULL);
		appendMate(staln, threadId, *rd2, rd1, rdid,
		           rs2, rs1, summ, *flags2, mapq, sc);
	}
}

/**
 * Extract one mate's alignment data into the per-thread columnar buffer.
 * Logic mirrors AlnSinkSam::appendMate() in aln_sink.cpp:1889 but stores
 * structured data instead of formatting SAM text.
 */
void AlnSinkColumnar::appendMate(
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
	const Scoring&    sc)
{
	/* Hard bounds check — assert is compiled out in Release builds (#6) */
	if(threadId >= thread_bufs_.size()) {
		/* Thread count exceeded expected nthreads. Resize under lock.
		   This handles cases where bowtie2 internally adjusts thread count
		   (e.g., OMP_NUM_THREADS). (#7) */
		thread_bufs_.resize(threadId + 1);
	}
	ColumnarThreadBuf& buf = thread_bufs_[threadId];

	if(rs != NULL) {
		staln.reset();
		rs->initStacked(rd, staln);
		staln.leftAlign(false);
	}

	/* QNAME — mirrors SamConfig::printReadName():
	   1. Truncate at first whitespace, cap at 255 chars
	   2. Strip trailing /1, /2, or /3 mate suffix if paired (#1) */
	{
		const char *name = rd.name.toZBuf();
		size_t namelen = rd.name.length();
		/* Truncate at whitespace and cap at 255 */
		size_t end = 0;
		while(end < namelen && end < 255 && !isspace((unsigned char)name[end])) {
			end++;
		}
		/* Strip /1, /2, /3 mate suffix */
		if(flags.partOfPair() && end >= 2 &&
		   name[end-2] == '/' &&
		   (name[end-1] == '1' || name[end-1] == '2' || name[end-1] == '3')) {
			end -= 2;
		}
		buf.qnames.push_back(std::string(name, end));
	}

	/* FLAG — mirrors aln_sink.cpp:1920-1948 */
	{
		int fl = 0;
		if(flags.partOfPair()) {
			fl |= SAM_FLAG_PAIRED;
			if(flags.alignedConcordant()) {
				fl |= SAM_FLAG_MAPPED_PAIRED;
			}
			if(!flags.mateAligned()) {
				fl |= SAM_FLAG_MATE_UNMAPPED;
			}
			fl |= (flags.readMate1() ?
				SAM_FLAG_FIRST_IN_PAIR : SAM_FLAG_SECOND_IN_PAIR);
			if(flags.mateAligned()) {
				bool oppFw = (rso != NULL) ? rso->fw() : flags.isOppFw();
				if(!oppFw) {
					fl |= SAM_FLAG_MATE_STRAND;
				}
			}
		}
		if(!flags.isPrimary()) {
			fl |= SAM_FLAG_NOT_PRIMARY;
		}
		if(rs != NULL && !rs->fw()) {
			fl |= SAM_FLAG_QUERY_STRAND;
		}
		if(rs == NULL) {
			fl |= SAM_FLAG_UNMAPPED;
		}
		buf.flags.push_back((int32_t)fl);
	}

	/* RNAME */
	if(rs != NULL) {
		buf.rnames.push_back(truncate_refname(refnames_[(size_t)rs->refid()]));
	} else if(summ.orefid() != -1) {
		buf.rnames.push_back(truncate_refname(refnames_[(size_t)summ.orefid()]));
	} else {
		buf.rnames.push_back("*");
	}

	/* POS */
	if(rs != NULL) {
		buf.positions.push_back((int64_t)(rs->refoff() + 1));
	} else if(summ.orefid() != -1) {
		buf.positions.push_back((int64_t)(summ.orefoff() + 1));
	} else {
		buf.positions.push_back(0);
	}

	/* MAPQ */
	if(rs != NULL) {
		char mapqInps[1024];
		mapqInps[0] = '\0';
		TMapq mq = mapqCalc.mapq(
			summ, flags, rd.mate < 2, rd.length(),
			rdo == NULL ? 0 : rdo->length(), mapqInps);
		buf.mapqs.push_back((uint8_t)mq);
	} else {
		buf.mapqs.push_back(0);
	}

	/* CIGAR */
	if(rs != NULL) {
		staln.buildCigar(flags.xeq());
		BTString cigar_buf;
		staln.writeCigar(&cigar_buf, NULL);
		buf.cigars.push_back(std::string(cigar_buf.toZBuf()));
	} else {
		buf.cigars.push_back("*");
	}

	/* RNEXT */
	if(rs != NULL && flags.partOfPair()) {
		if(rso != NULL && rs->refid() != rso->refid()) {
			buf.rnexts.push_back(truncate_refname(refnames_[(size_t)rso->refid()]));
		} else {
			buf.rnexts.push_back("=");
		}
	} else if(summ.orefid() != -1) {
		buf.rnexts.push_back("=");
	} else {
		buf.rnexts.push_back("*");
	}

	/* PNEXT */
	if(rs != NULL && flags.partOfPair()) {
		if(rso != NULL) {
			buf.pnexts.push_back((int64_t)(rso->refoff() + 1));
		} else {
			buf.pnexts.push_back((int64_t)(rs->refoff() + 1));
		}
	} else if(summ.orefid() != -1) {
		buf.pnexts.push_back((int64_t)(summ.orefoff() + 1));
	} else {
		buf.pnexts.push_back(0);
	}

	/* TLEN */
	if(rs != NULL && rs->isFraglenSet()) {
		buf.tlens.push_back((int64_t)rs->fragmentLength());
	} else {
		buf.tlens.push_back(0);
	}

	/* SEQ — always emit actual sequence, even for secondary alignments.
	   The SAM path may output "*" for secondaries when omitSecondarySeqQual
	   is set, but the API always provides full data since callers typically
	   want access to the sequence regardless of primary/secondary status. (#2) */
	if(rs == NULL || rs->fw()) {
		if(rd.patFw.length() == 0) {
			buf.seqs.push_back("*");
		} else {
			buf.seqs.push_back(std::string(rd.patFw.toZBuf()));
		}
	} else {
		buf.seqs.push_back(std::string(rd.patRc.toZBuf()));
	}

	/* QUAL */
	if(rs == NULL || rs->fw()) {
		if(rd.qual.length() == 0) {
			buf.quals.push_back("*");
		} else {
			buf.quals.push_back(std::string(rd.qual.toZBuf()));
		}
	} else {
		buf.quals.push_back(std::string(rd.qualRev.toZBuf()));
	}

	/* --- Optional tags --- */

	/* AS:i — alignment score */
	if(rs != NULL) {
		buf.tag_as.push_back((int32_t)rs->score().score());
	} else {
		buf.tag_as.push_back(0);
	}

	/* XS:i — second-best score. INT32_MIN means absent (unique alignment
	   with no valid second-best score). The SAM path omits the tag entirely
	   in this case; we use a sentinel so callers can distinguish absent from
	   score=0. (#5) */
	if(rs != NULL) {
		AlnScore sco;
		if(flags.partOfPair()) {
			sco = summ.bestUnchosenPScore(rd.mate < 2);
		} else {
			sco = summ.bestUnchosenUScore();
		}
		buf.tag_xs.push_back(sco.valid() ? (int32_t)sco.score() : INT32_MIN);
	} else {
		buf.tag_xs.push_back(INT32_MIN);
	}

	/* NM:i — edit distance */
	if(rs != NULL) {
		buf.tag_nm.push_back((int32_t)rs->ned().size());
	} else {
		buf.tag_nm.push_back(0);
	}

	/* MD:Z — mismatch string */
	if(rs != NULL) {
		staln.buildMdz();
		BTString mdz_buf;
		staln.writeMdz(&mdz_buf, NULL);
		buf.tag_md.push_back(std::string(mdz_buf.toZBuf()));
	} else {
		buf.tag_md.push_back("");
	}

	/* YT:Z — alignment type */
	{
		const char *yt = "";
		if(flags.alignedConcordant()) {
			yt = "CP";
		} else if(flags.alignedDiscordant()) {
			yt = "DP";
		} else if(flags.alignedUnpairedMate()) {
			yt = "UP";
		} else {
			yt = "UU";
		}
		buf.tag_yt.push_back(yt);
	}

	/* Read ID for ordering */
	buf.rdids.push_back((uint64_t)rdid);
}

size_t AlnSinkColumnar::numRecords() const {
	size_t total = 0;
	for(size_t i = 0; i < thread_bufs_.size(); i++) {
		total += thread_bufs_[i].size();
	}
	return total;
}

/**
 * Merge per-thread buffers and pack into single allocation.
 */
bt2_align_output_t *AlnSinkColumnar::finalize() {
	size_t n = numRecords();

	if(n == 0) {
		bt2_align_output_t *out = (bt2_align_output_t *)calloc(1, sizeof(bt2_align_output_t));
		if(!out) return NULL;
		out->struct_size = sizeof(bt2_align_output_t);
		out->n_records = 0;
		out->_backing = out;
		return out;
	}

	/* Merge all threads into a single index-sorted list.
	   Build (rdid, thread_idx, record_idx) tuples and sort by rdid
	   to match native bowtie2 output order. For records with the same
	   rdid (paired mates, secondary alignments), preserve insertion order. */
	struct RecordRef {
		uint64_t rdid;
		size_t thread_idx;
		size_t record_idx;
	};
	std::vector<RecordRef> refs;
	refs.reserve(n);
	for(size_t t = 0; t < thread_bufs_.size(); t++) {
		for(size_t r = 0; r < thread_bufs_[t].size(); r++) {
			refs.push_back({thread_bufs_[t].rdids[r], t, r});
		}
	}
	std::stable_sort(refs.begin(), refs.end(),
		[](const RecordRef& a, const RecordRef& b) {
			return a.rdid < b.rdid;
		});

	/* Compute total string bytes */
	size_t str_bytes = 0;
	for(size_t i = 0; i < n; i++) {
		const ColumnarThreadBuf& tb = thread_bufs_[refs[i].thread_idx];
		size_t r = refs[i].record_idx;
		str_bytes += tb.qnames[r].size() + 1;
		str_bytes += tb.rnames[r].size() + 1;
		str_bytes += tb.cigars[r].size() + 1;
		str_bytes += tb.rnexts[r].size() + 1;
		str_bytes += tb.seqs[r].size() + 1;
		str_bytes += tb.quals[r].size() + 1;
		str_bytes += tb.tag_md[r].size() + 1;
		str_bytes += tb.tag_yt[r].size() + 1;
	}

	/* Single allocation with alignment padding.
	   Pad between sections for proper alignment of int32_t and int64_t
	   arrays. Worst-case padding is (alignment - 1) bytes per boundary. (#4) */
	size_t ptr_bytes = 8 * n * sizeof(const char *);
	size_t i32_bytes = 4 * n * sizeof(int32_t);
	size_t i64_bytes = 3 * n * sizeof(int64_t);
	size_t u8_bytes  = n * sizeof(uint8_t);
	size_t align_pad = (alignof(int32_t) - 1) + (alignof(int64_t) - 1);
	size_t total = sizeof(bt2_align_output_t) + ptr_bytes +
	               i32_bytes + i64_bytes + u8_bytes + str_bytes + align_pad;

	char *block = (char *)calloc(1, total);
	if(!block) return NULL;

	bt2_align_output_t *out = (bt2_align_output_t *)block;
	char *cursor = block + sizeof(bt2_align_output_t);

	/* Pointer arrays */
	out->qname  = (const char **)cursor; cursor += n * sizeof(const char *);
	out->rname  = (const char **)cursor; cursor += n * sizeof(const char *);
	out->cigar  = (const char **)cursor; cursor += n * sizeof(const char *);
	out->rnext  = (const char **)cursor; cursor += n * sizeof(const char *);
	out->seq    = (const char **)cursor; cursor += n * sizeof(const char *);
	out->qual   = (const char **)cursor; cursor += n * sizeof(const char *);
	out->tag_md = (const char **)cursor; cursor += n * sizeof(const char *);
	out->tag_yt = (const char **)cursor; cursor += n * sizeof(const char *);

	/* int32_t arrays — align */
	{
		uintptr_t addr = (uintptr_t)cursor;
		cursor = (char *)((addr + alignof(int32_t) - 1) & ~(alignof(int32_t) - 1));
	}
	out->flag   = (int32_t *)cursor; cursor += n * sizeof(int32_t);
	out->tag_as = (int32_t *)cursor; cursor += n * sizeof(int32_t);
	out->tag_xs = (int32_t *)cursor; cursor += n * sizeof(int32_t);
	out->tag_nm = (int32_t *)cursor; cursor += n * sizeof(int32_t);

	/* int64_t arrays — align */
	{
		uintptr_t addr = (uintptr_t)cursor;
		cursor = (char *)((addr + alignof(int64_t) - 1) & ~(alignof(int64_t) - 1));
	}
	out->pos   = (int64_t *)cursor; cursor += n * sizeof(int64_t);
	out->pnext = (int64_t *)cursor; cursor += n * sizeof(int64_t);
	out->tlen  = (int64_t *)cursor; cursor += n * sizeof(int64_t);

	/* uint8_t */
	out->mapq = (uint8_t *)cursor; cursor += n * sizeof(uint8_t);

	/* String buffer */
	char *strpos = cursor;

	#define PACK_STR(dst_arr, idx, src_str) do { \
		size_t _slen = (src_str).size(); \
		memcpy(strpos, (src_str).data(), _slen); \
		strpos[_slen] = '\0'; \
		(dst_arr)[(idx)] = strpos; \
		strpos += _slen + 1; \
	} while(0)

	for(size_t i = 0; i < n; i++) {
		const ColumnarThreadBuf& tb = thread_bufs_[refs[i].thread_idx];
		size_t r = refs[i].record_idx;

		PACK_STR(out->qname, i, tb.qnames[r]);
		out->flag[i]  = tb.flags[r];
		PACK_STR(out->rname, i, tb.rnames[r]);
		out->pos[i]   = tb.positions[r];
		out->mapq[i]  = tb.mapqs[r];
		PACK_STR(out->cigar, i, tb.cigars[r]);
		PACK_STR(out->rnext, i, tb.rnexts[r]);
		out->pnext[i] = tb.pnexts[r];
		out->tlen[i]  = tb.tlens[r];
		PACK_STR(out->seq,  i, tb.seqs[r]);
		PACK_STR(out->qual, i, tb.quals[r]);
		out->tag_as[i] = tb.tag_as[r];
		out->tag_xs[i] = tb.tag_xs[r];
		out->tag_nm[i] = tb.tag_nm[r];
		PACK_STR(out->tag_md, i, tb.tag_md[r]);
		PACK_STR(out->tag_yt, i, tb.tag_yt[r]);
	}

	#undef PACK_STR

	out->struct_size = sizeof(bt2_align_output_t);
	out->n_records = n;
	out->_backing = block;

	return out;
}
