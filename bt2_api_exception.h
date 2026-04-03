/*
 * Exception type for the bowtie2 C API library path.
 * Carries a BT2_ERR_* error code so catch blocks can map
 * to specific API error returns. Only used when BT2_NO_MAIN
 * is defined (library builds).
 */

#ifndef BT2_API_EXCEPTION_H
#define BT2_API_EXCEPTION_H

#include <stdexcept>
#include <string>

class Bt2ApiException : public std::runtime_error {
public:
	int error_code; /* BT2_ERR_* constant from bt2_api.h */

	Bt2ApiException(int code, const std::string& msg)
		: std::runtime_error(msg), error_code(code) {}
};

#endif /* BT2_API_EXCEPTION_H */
