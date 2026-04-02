/*
 * SAM text parser for the bowtie2 C API.
 */

#include "bt2_sam_parse.h"
#include <cstdlib>
#include <cstring>
#include <cstdint>

/* Parse a single optional tag value from SAM optional fields.
   opt_start points to the first optional field (tab-separated).
   Returns pointer to value string (within the line), or NULL if not found. */
static const char *find_tag(const char *opt_start, const char *tag_name) {
	const char *p = opt_start;
	size_t tag_len = strlen(tag_name); /* e.g. "AS:i:" is 5 chars */
	while (p && *p) {
		if (strncmp(p, tag_name, tag_len) == 0) {
			return p + tag_len;
		}
		/* advance to next tab or end */
		p = strchr(p, '\t');
		if (p) p++;
	}
	return NULL;
}

/* Count non-header, non-empty lines */
static size_t count_records(const char *text) {
	size_t count = 0;
	const char *p = text;
	while (*p) {
		/* Skip empty lines and header lines */
		if (*p != '@' && *p != '\n' && *p != '\r') {
			count++;
		}
		const char *nl = strchr(p, '\n');
		if (!nl) {
			/* Last line without trailing newline — already counted if non-empty */
			break;
		}
		p = nl + 1;
	}
	return count;
}

/* Compute total string bytes needed for all string fields */
static size_t compute_string_bytes(const char *text) {
	size_t bytes = 0;
	const char *p = text;
	while (*p) {
		if (*p != '@' && *p != '\n' && *p != '\r') {
			const char *nl = strchr(p, '\n');
			size_t line_len = nl ? (size_t)(nl - p) : strlen(p);
			bytes += line_len + 64; /* extra for null terminators */
		}
		const char *nl = strchr(p, '\n');
		if (!nl) break;
		p = nl + 1;
	}
	return bytes;
}

/* Align cursor up to the given alignment boundary */
static char *align_up(char *ptr, size_t alignment) {
	uintptr_t addr = (uintptr_t)ptr;
	uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);
	return (char *)aligned;
}

int bt2_parse_sam(char *sam_text, size_t sam_len,
                  bt2_align_output_t **output_out) {
	if (!sam_text || !output_out) return BT2_ERR_INPUT;
	(void)sam_len;

	size_t n = count_records(sam_text);

	if (n == 0) {
		/* Valid: no alignment records. _backing == out for single free. */
		bt2_align_output_t *out = (bt2_align_output_t *)calloc(1, sizeof(bt2_align_output_t));
		if (!out) return BT2_ERR_NOMEM;
		out->struct_size = sizeof(bt2_align_output_t);
		out->n_records = 0;
		out->_backing = out;
		*output_out = out;
		return BT2_OK;
	}

	size_t str_bytes = compute_string_bytes(sam_text);

	/* Single allocation layout with alignment padding between sections:
	   [bt2_align_output_t]       (aligned by calloc)
	   [8 x const char*[n]]      (pointer-aligned)
	   [padding to 4-byte]
	   [4 x int32_t[n]]          (4-byte aligned)
	   [padding to 8-byte]
	   [3 x int64_t[n]]          (8-byte aligned)
	   [1 x uint8_t[n]]          (no alignment needed)
	   [string buffer]
	*/
	size_t ptr_bytes = 8 * n * sizeof(const char *);
	size_t i32_bytes = 4 * n * sizeof(int32_t);
	size_t i64_bytes = 3 * n * sizeof(int64_t);
	size_t u8_bytes  = 1 * n * sizeof(uint8_t);
	/* Over-allocate for alignment padding (at most 7 bytes per boundary) */
	size_t total = sizeof(bt2_align_output_t) + ptr_bytes + 8 +
	               i32_bytes + 8 + i64_bytes + u8_bytes + str_bytes;

	char *block = (char *)calloc(1, total);
	if (!block) return BT2_ERR_NOMEM;

	bt2_align_output_t *out = (bt2_align_output_t *)block;
	char *cursor = block + sizeof(bt2_align_output_t);

	/* Pointer arrays (already aligned by struct alignment) */
	out->qname  = (const char **)cursor; cursor += n * sizeof(const char *);
	out->rname  = (const char **)cursor; cursor += n * sizeof(const char *);
	out->cigar  = (const char **)cursor; cursor += n * sizeof(const char *);
	out->rnext  = (const char **)cursor; cursor += n * sizeof(const char *);
	out->seq    = (const char **)cursor; cursor += n * sizeof(const char *);
	out->qual   = (const char **)cursor; cursor += n * sizeof(const char *);
	out->tag_md = (const char **)cursor; cursor += n * sizeof(const char *);
	out->tag_yt = (const char **)cursor; cursor += n * sizeof(const char *);

	/* int32_t arrays — align to 4 bytes */
	cursor = align_up(cursor, alignof(int32_t));
	out->flag   = (int32_t *)cursor; cursor += n * sizeof(int32_t);
	out->tag_as = (int32_t *)cursor; cursor += n * sizeof(int32_t);
	out->tag_xs = (int32_t *)cursor; cursor += n * sizeof(int32_t);
	out->tag_nm = (int32_t *)cursor; cursor += n * sizeof(int32_t);

	/* int64_t arrays — align to 8 bytes */
	cursor = align_up(cursor, alignof(int64_t));
	out->pos   = (int64_t *)cursor; cursor += n * sizeof(int64_t);
	out->pnext = (int64_t *)cursor; cursor += n * sizeof(int64_t);
	out->tlen  = (int64_t *)cursor; cursor += n * sizeof(int64_t);

	/* uint8_t array — no alignment needed */
	out->mapq  = (uint8_t *)cursor; cursor += n * sizeof(uint8_t);

	/* String buffer */
	char *strpos = cursor;
	char *strend = block + total;

	/* Pass 2: parse records */
	size_t idx = 0;
	char *line = sam_text;
	while (*line && idx < n) {
		/* Skip header and empty lines */
		if (*line == '@' || *line == '\n' || *line == '\r') {
			char *nl = strchr(line, '\n');
			line = nl ? nl + 1 : line + strlen(line);
			continue;
		}

		char *nl = strchr(line, '\n');
		if (nl) *nl = '\0';

		/* Tab-split: null-terminate the first 11 mandatory fields.
		   Optional tags after field 11 are left intact for find_tag. */
		char *fields[12];
		int nf = 0;
		char *p = line;
		while (nf < 11 && p) {
			fields[nf++] = p;
			p = strchr(p, '\t');
			if (p) *p++ = '\0';
		}
		if (p) fields[nf++] = p; /* start of optional tags */

		if (nf < 11) {
			line = nl ? nl + 1 : line + strlen(line);
			continue;
		}

		/* Helper: copy string into backing buffer */
		#define COPY_STR(dst, src) do { \
			size_t _len = strlen(src); \
			if (strpos + _len + 1 > strend) goto fail; \
			memcpy(strpos, src, _len + 1); \
			(dst) = strpos; \
			strpos += _len + 1; \
		} while(0)

		COPY_STR(out->qname[idx], fields[0]);
		out->flag[idx]  = (int32_t)strtol(fields[1], NULL, 10);
		COPY_STR(out->rname[idx], fields[2]);
		out->pos[idx]   = (int64_t)strtoll(fields[3], NULL, 10);
		out->mapq[idx]  = (uint8_t)strtoul(fields[4], NULL, 10);
		COPY_STR(out->cigar[idx], fields[5]);
		COPY_STR(out->rnext[idx], fields[6]);
		out->pnext[idx] = (int64_t)strtoll(fields[7], NULL, 10);
		out->tlen[idx]  = (int64_t)strtoll(fields[8], NULL, 10);
		COPY_STR(out->seq[idx], fields[9]);
		COPY_STR(out->qual[idx], fields[10]);

		/* Optional tags */
		const char *opt_start = (nf >= 12) ? fields[11] : "";
		const char *val;

		val = find_tag(opt_start, "AS:i:");
		out->tag_as[idx] = val ? (int32_t)strtol(val, NULL, 10) : 0;

		val = find_tag(opt_start, "XS:i:");
		out->tag_xs[idx] = val ? (int32_t)strtol(val, NULL, 10) : 0;

		val = find_tag(opt_start, "NM:i:");
		out->tag_nm[idx] = val ? (int32_t)strtol(val, NULL, 10) : 0;

		val = find_tag(opt_start, "MD:Z:");
		if (val) {
			size_t vlen = 0;
			while (val[vlen] && val[vlen] != '\t') vlen++;
			if (strpos + vlen + 1 > strend) goto fail;
			memcpy(strpos, val, vlen);
			strpos[vlen] = '\0';
			out->tag_md[idx] = strpos;
			strpos += vlen + 1;
		} else {
			out->tag_md[idx] = NULL;
		}

		val = find_tag(opt_start, "YT:Z:");
		if (val) {
			size_t vlen = 0;
			while (val[vlen] && val[vlen] != '\t') vlen++;
			if (strpos + vlen + 1 > strend) goto fail;
			memcpy(strpos, val, vlen);
			strpos[vlen] = '\0';
			out->tag_yt[idx] = strpos;
			strpos += vlen + 1;
		} else {
			out->tag_yt[idx] = NULL;
		}

		#undef COPY_STR

		idx++;
		line = nl ? nl + 1 : line + strlen(line);
	}

	out->struct_size = sizeof(bt2_align_output_t);
	out->n_records = idx;
	out->_backing = block; /* _backing owns the entire allocation including *out */

	*output_out = out;
	return BT2_OK;

fail:
	free(block);
	return BT2_ERR_NOMEM;
}
