/*
 * Internal header: exposes driver() to bt2_api.cpp for direct calls.
 * Not part of the public C API.
 */

#ifndef BT2_DRIVER_API_H
#define BT2_DRIVER_API_H

#include <string>

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
 * Set statics from API config. Must be called under g_bowtie_mutex.
 * Calls resetOptions() first for a clean slate.
 */
void apply_config_to_statics(int nthreads, int64_t seed, int quiet,
                             int preset, int local_align);

/*
 * Returns true if the index at base path is a large (64-bit) index.
 */
bool bt2_index_is_large(const std::string& base);

#endif /* BT2_DRIVER_API_H */
