/*
 * Custom std::streambuf that redirects cerr output to a bt2_log_fn
 * callback. Buffers characters until newline, then delivers the
 * accumulated line to the callback.
 *
 * Level heuristic:
 *   - Lines starting with "Error:" or "ERROR:" -> BT2_LOG_ERROR
 *   - Lines starting with "Warning:" -> BT2_LOG_WARN
 *   - All other lines -> BT2_LOG_INFO
 *
 * Thread safety: must be installed inside g_bowtie_mutex scope.
 * The API sets --quiet, so worker threads should not write to cerr.
 */

#ifndef BT2_LOG_STREAMBUF_H
#define BT2_LOG_STREAMBUF_H

#include "bt2_api.h"
#include <iostream>
#include <mutex>
#include <memory>
#include <streambuf>
#include <string>
#include <cstring>

/**
 * Thread-safe streambuf that routes cerr output to a bt2_log_fn callback.
 * All overflow/sync calls are mutex-protected so worker threads that
 * write to cerr (e.g. on error conditions) do not cause data races.
 */
class LogCallbackStreambuf : public std::streambuf {
public:
	LogCallbackStreambuf(bt2_log_fn fn, void *user_data)
		: fn_(fn), user_data_(user_data) {}

protected:
	int overflow(int c) override {
		std::lock_guard<std::mutex> lock(mtx_);
		if (c != traits_type::eof()) {
			char ch = static_cast<char>(c);
			buffer_ += ch;
			if (ch == '\n') {
				flush_line();
			}
		}
		return c;
	}

	int sync() override {
		std::lock_guard<std::mutex> lock(mtx_);
		if (!buffer_.empty()) {
			flush_line();
		}
		return 0;
	}

private:
	void flush_line() {
		if (!fn_ || buffer_.empty()) {
			buffer_.clear();
			return;
		}
		/* Strip trailing newline for cleaner callback messages */
		while (!buffer_.empty() &&
		       (buffer_.back() == '\n' || buffer_.back() == '\r')) {
			buffer_.pop_back();
		}
		if (buffer_.empty()) {
			return;
		}

		/* Best-effort level detection from message prefix.
		   Not exhaustive — messages without "Error:"/"Warning:" prefix
		   (e.g. alignment summary lines) are classified as INFO. */
		int level = BT2_LOG_INFO;
		if (buffer_.compare(0, 6, "Error:") == 0 ||
		    buffer_.compare(0, 6, "ERROR:") == 0) {
			level = BT2_LOG_ERROR;
		} else if (buffer_.compare(0, 8, "Warning:") == 0) {
			level = BT2_LOG_WARN;
		}

		fn_(user_data_, level, buffer_.c_str());
		buffer_.clear();
	}

	bt2_log_fn fn_;
	void *user_data_;
	std::string buffer_;
	std::mutex mtx_;  /* protects buffer_ and callback invocation */
};

/*
 * Null streambuf — discards all output. Used when quiet=1 and log_fn=NULL.
 */
class NullStreambuf : public std::streambuf {
protected:
	int overflow(int c) override { return c; }
};

/*
 * RAII guard: redirects cerr to a LogCallbackStreambuf (or NullStreambuf)
 * for the duration of its lifetime. Restores the original streambuf
 * on destruction (including exception unwind).
 */
class CerrRedirectGuard {
public:
	CerrRedirectGuard(bt2_log_fn fn, void *user_data, bool quiet)
		: orig_(std::cerr.rdbuf()) {
		if (fn) {
			buf_.reset(new LogCallbackStreambuf(fn, user_data));
			std::cerr.rdbuf(buf_.get());
		} else if (quiet) {
			null_buf_.reset(new NullStreambuf());
			std::cerr.rdbuf(null_buf_.get());
		}
	}

	~CerrRedirectGuard() {
		std::cerr.rdbuf(orig_);
	}

	CerrRedirectGuard(const CerrRedirectGuard&) = delete;
	CerrRedirectGuard& operator=(const CerrRedirectGuard&) = delete;

private:
	std::streambuf *orig_;
	std::unique_ptr<LogCallbackStreambuf> buf_;
	std::unique_ptr<NullStreambuf> null_buf_;
};

#endif /* BT2_LOG_STREAMBUF_H */
