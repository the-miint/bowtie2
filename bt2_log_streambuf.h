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
 * Thread-safe streambuf that routes cerr (or cout) output to a bt2_log_fn
 * callback.
 * All overflow/sync calls are mutex-protected so worker threads that
 * write to cerr (e.g. on error conditions) do not cause data races.
 */
class LogCallbackStreambuf : public std::streambuf {
public:
	LogCallbackStreambuf(bt2_log_fn fn, void *user_data)
		: fn_(fn), user_data_(user_data) {}

	~LogCallbackStreambuf() {
		/* Flush any partial line remaining in the buffer */
		if (!buffer_.empty()) {
			flush_line();
		}
	}

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
 * RAII guard: redirects cerr (and optionally cout) to a
 * LogCallbackStreambuf or NullStreambuf for the duration of its
 * lifetime. Restores the original streambufs on destruction.
 *
 * Uses a shared static mutex to prevent concurrent stream redirection
 * from aligner and builder paths.
 *
 * redirect_cout: when true, also captures std::cout. Use for builder
 * API (bowtie2-build writes progress to cout). Leave false for aligner
 * (SAM output goes through OutFileBuf, not cout).
 */
class CerrRedirectGuard {
public:
	CerrRedirectGuard(bt2_log_fn fn, void *user_data, bool quiet,
	                   bool redirect_cout = false)
		: lock_(), orig_cerr_(NULL), orig_cout_(NULL),
		  redirect_cout_(redirect_cout) {
		bool needs_redirect = (fn != NULL) || quiet;
		if (needs_redirect) {
			lock_ = std::unique_lock<std::mutex>(g_cerr_redirect_mtx_);
		}

		orig_cerr_ = std::cerr.rdbuf();
		if (redirect_cout_) orig_cout_ = std::cout.rdbuf();

		if (fn) {
			cerr_buf_.reset(new LogCallbackStreambuf(fn, user_data));
			std::cerr.rdbuf(cerr_buf_.get());
			if (redirect_cout_) {
				cout_buf_.reset(new LogCallbackStreambuf(fn, user_data));
				std::cout.rdbuf(cout_buf_.get());
			}
		} else if (quiet) {
			null_cerr_.reset(new NullStreambuf());
			std::cerr.rdbuf(null_cerr_.get());
			if (redirect_cout_) {
				null_cout_.reset(new NullStreambuf());
				std::cout.rdbuf(null_cout_.get());
			}
		}
	}

	~CerrRedirectGuard() {
		std::cerr.rdbuf(orig_cerr_);
		if (redirect_cout_ && orig_cout_) std::cout.rdbuf(orig_cout_);
		/* lock_ releases automatically via unique_lock destructor */
	}

	CerrRedirectGuard(const CerrRedirectGuard&) = delete;
	CerrRedirectGuard& operator=(const CerrRedirectGuard&) = delete;

private:
	static std::mutex g_cerr_redirect_mtx_;
	std::unique_lock<std::mutex> lock_; /* only held when redirecting */
	std::streambuf *orig_cerr_;
	std::streambuf *orig_cout_;
	bool redirect_cout_;
	std::unique_ptr<LogCallbackStreambuf> cerr_buf_;
	std::unique_ptr<LogCallbackStreambuf> cout_buf_;
	std::unique_ptr<NullStreambuf> null_cerr_;
	std::unique_ptr<NullStreambuf> null_cout_;
};

#endif /* BT2_LOG_STREAMBUF_H */
