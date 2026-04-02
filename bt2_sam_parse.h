/*
 * SAM text parser for the bowtie2 C API.
 *
 * Parses SAM text output into bt2_align_output_t SOA structure with a
 * single backing allocation. Used by Phase 1 to capture bowtie2 output;
 * will be replaced by direct columnar capture in Phase 2.
 */

#ifndef BT2_SAM_PARSE_H
#define BT2_SAM_PARSE_H

#include "bt2_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Parse SAM text into a bt2_align_output_t with a single backing allocation.
 * Skips @-header lines. Parses all 11 mandatory SAM fields plus optional
 * tags AS:i, XS:i, NM:i, MD:Z, YT:Z.
 *
 * @param sam_text   Null-terminated SAM text (may be modified during parsing).
 * @param sam_len    Length of sam_text in bytes.
 * @param output_out On success, receives a pointer to the output.
 * @return BT2_OK on success, negative error code on failure.
 */
int bt2_parse_sam(char *sam_text, size_t sam_len,
                  bt2_align_output_t **output_out);

#ifdef __cplusplus
}
#endif

#endif /* BT2_SAM_PARSE_H */
