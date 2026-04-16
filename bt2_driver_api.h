/*
 * Internal header: exposes driver() to bt2_api.cpp for direct calls.
 * Not part of the public C API.
 */

#ifndef BT2_DRIVER_API_H
#define BT2_DRIVER_API_H

#include <string>
#include "bt2_api.h"

class PatternComposer;
class AlnSink;

/*
 * Call driver<uint32_t>() (small index) with optional injected
 * PatternComposer and AlnSink. When non-NULL, driver() uses them
 * instead of creating its own from statics. Caller retains ownership.
 */
void driver_api_small(
	const std::string& bt2indexBase,
	const std::string& outfile,
	PatternComposer *api_patsrc,
	AlnSink *api_sink);

/*
 * Call driver<uint64_t>() (large index) with the same semantics.
 */
void driver_api_large(
	const std::string& bt2indexBase,
	const std::string& outfile,
	PatternComposer *api_patsrc,
	AlnSink *api_sink);

/*
 * Set statics from API config. Calls resetOptions() first for a clean slate,
 * then builds an argv from config fields and calls parseOptions().
 *
 * THREAD SAFETY: writes to global statics (opterr, optind, all bowtie2
 * option globals). Caller MUST hold g_bowtie_mutex. This function takes
 * no lock itself.
 */
void apply_config_to_statics(const bt2_align_config_t *config,
                             int effective_quiet);

/*
 * Returns true if the index at base path is a large (64-bit) index.
 */
bool bt2_index_is_large(const std::string& base);

#endif /* BT2_DRIVER_API_H */
