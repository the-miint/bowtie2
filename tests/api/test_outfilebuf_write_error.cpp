/*
 * OutFileBuf and a failed write, with its writer thread (async) and without
 * it (async = false, as the C API runs at nthreads=1):
 *   - close() throws, with the file closed. Destroying the buffer after
 *     that, or without closing it, neither throws (a throw out of a
 *     destructor ends the process) nor leaves its file open.
 *   - The failed write hands its buffer back, so a flush() after it, the
 *     writes after that and close() all return.
 *   - Each failure reaches the API's log_fn once, as one BT2_LOG_ERROR line.
 *   - It fails wherever the write does: in fwrite (more than stdio
 *     buffers), in fclose (less), or partway (a file-size limit cuts the
 *     write short, and the stream then counts what it only buffers as
 *     written).
 *   - A write to a pipe whose reading end has closed (EPIPE, with SIGPIPE
 *     ignored) ends the output without an error: close() does not throw.
 *
 * /dev/full fails every write with ENOSPC; its checks skip where there is
 * none (macOS). In WebAssembly only the no-writer /dev/full checks run,
 * without the descriptor count (/dev/fd is the host's there).
 */
#include "filebuf.h"
#include "bt2_log_streambuf.h"
#include "test_helpers.h"
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <unistd.h>

static int g_failures = 0;

static const std::string BIG(1 << 20, 'x'); // more than stdio buffers
static const std::string SMALL(100, 'x');   // less

/* What log_fn got: every call, and the calls that were the whole error line
   for errno err at BT2_LOG_ERROR. */
struct LogSeen {
    explicit LogSeen(int err)
        : want(std::string("Error: could not write the output: ") +
               std::strerror(err)),
          calls(0), whole_errors(0) {}
    std::string want;
    int calls;
    int whole_errors;
};

static void log_fn(void *user_data, int level, const char *msg) {
    LogSeen *seen = static_cast<LogSeen *>(user_data);
    seen->calls++;
    if (level == BT2_LOG_ERROR && seen->want == msg) seen->whole_errors++;
}

static void check_log(const char *mode, const LogSeen &seen, int want) {
    CHECK_MSG(seen.calls == want && seen.whole_errors == want, "[%s] log_fn got "
              "%d call(s), %d of them \"%s\" at BT2_LOG_ERROR; want %d and %d",
              mode, seen.calls, seen.whole_errors, seen.want.c_str(), want, want);
}

/* Open file descriptors now. Outside WebAssembly not being able to count
   them fails the test. */
static int fds_now(const char *mode) {
    int n = count_open_fds();
#ifndef __EMSCRIPTEN__
    CHECK_MSG(n >= 0, "[%s] open file descriptors could not be counted", mode);
#endif
    return n;
}

/* close(), as the driver calls it: whether it threw. */
static bool close_threw(OutFileBuf &ob) {
    try {
        ob.close();
    } catch (const std::exception &) {
        return true;
    }
    return false;
}

static void check_full(bool async) {
    const char *mode = async ? "writer thread" : "no writer thread";
    int fds_before = fds_now(mode);
    LogSeen seen(ENOSPC);
    {
        CerrRedirectGuard guard(log_fn, &seen, false);
        {
            OutFileBuf ob("/dev/full", false, async);
            ob.writeString(BIG);
            CHECK_MSG(close_threw(ob), "[%s] close() after a failed write did not "
                      "throw", mode);
            CHECK_MSG(ob.closed(), "[%s] close() threw but left the file open", mode);
        } // destroyed after close() threw: must not throw again
        {
            OutFileBuf ob("/dev/full", false, async);
            ob.writeString(BIG);
        } // destroyed unclosed: its close() fails, and must not throw
        {
            OutFileBuf ob("/dev/full", false, async);
            ob.writeString(BIG);
            ob.flush();          // fails
            ob.writeString(BIG); // dropped
            ob.flush();
            CHECK_MSG(close_threw(ob), "[%s] close() after a failed flush() did not "
                      "throw", mode);
        }
        {
            OutFileBuf ob("/dev/full", false, async);
            ob.writeString(SMALL); // fwrite only buffers it; fclose fails
            CHECK_MSG(close_threw(ob), "[%s] close() did not throw when fclose() "
                      "failed to write", mode);
        }
    }
    check_log(mode, seen, 4); // one per buffer above
    int fds_after = fds_now(mode);
    CHECK_MSG(fds_after == fds_before, "[%s] open file descriptors went from %d "
              "to %d", mode, fds_before, fds_after);
}

#ifndef __EMSCRIPTEN__
static void check_partial_write(bool async) {
    const char *mode = async ? "writer thread, partial write"
                             : "no writer thread, partial write";
    const std::string path =
        std::string(TEST_SCRATCH_DIR) + "/outfilebuf_partial_write.out";
    struct rlimit unlimited, limited;
    if (getrlimit(RLIMIT_FSIZE, &unlimited) != 0) {
        CHECK_MSG(0, "[%s] getrlimit(RLIMIT_FSIZE): %s", mode, std::strerror(errno));
        return;
    }
    limited = unlimited;
    limited.rlim_cur = BIG.size() / 10; // the write gets this far, then EFBIG
    LogSeen seen(EFBIG);
    bool threw = false;
    {
        CerrRedirectGuard guard(log_fn, &seen, false);
        OutFileBuf ob(path.c_str(), false, async);
        CHECK_MSG(setrlimit(RLIMIT_FSIZE, &limited) == 0, "[%s] setrlimit: %s",
                  mode, std::strerror(errno));
        ob.writeString(BIG);
        threw = close_threw(ob);
        setrlimit(RLIMIT_FSIZE, &unlimited);
    }
    std::remove(path.c_str());
    CHECK_MSG(threw, "[%s] close() did not throw after a write the file-size "
              "limit cut short", mode);
    check_log(mode, seen, 1);
}

static void check_closed_pipe(bool async) {
    const char *mode = async ? "writer thread, closed pipe"
                             : "no writer thread, closed pipe";
    int fds_before = fds_now(mode);
    int fd[2];
    if (pipe(fd) != 0) {
        CHECK_MSG(0, "[%s] pipe(): %s", mode, std::strerror(errno));
        return;
    }
    char path[64];
    std::snprintf(path, sizeof(path), "/dev/fd/%d", fd[1]);
    {
        /* Opened while the pipe has a reader: on Linux, opening a pipe
           through /dev/fd with none waits for one. */
        OutFileBuf ob(path, false, async);
        close(fd[0]);
        ob.writeString(BIG);
        ob.flush();          // EPIPE
        ob.writeString(BIG); // dropped
        CHECK_MSG(!close_threw(ob), "[%s] close() threw; a closed pipe ends the "
                  "output without an error", mode);
    }
    close(fd[1]);
    int fds_after = fds_now(mode);
    CHECK_MSG(fds_after == fds_before, "[%s] open file descriptors went from %d "
              "to %d", mode, fds_before, fds_after);
}
#endif

int main() {
    FILE *probe = std::fopen("/dev/full", "w");
    if (probe != NULL) {
        std::fclose(probe);
        check_full(false);
#if TEST_HAS_THREADS
        check_full(true);
#endif
    } else {
#ifdef __EMSCRIPTEN__
        std::printf("test_outfilebuf_write_error: SKIP (no /dev/full)\n");
        return TEST_SKIP;
#else
        std::printf("test_outfilebuf_write_error: /dev/full checks skipped (no "
                    "/dev/full)\n");
#endif
    }
#ifndef __EMSCRIPTEN__
    std::signal(SIGPIPE, SIG_IGN); // the write gets EPIPE instead
    std::signal(SIGXFSZ, SIG_IGN); // the write gets EFBIG instead
    check_partial_write(false);
    check_partial_write(true);
    check_closed_pipe(false);
    check_closed_pipe(true);
#endif
    if (g_failures != 0) {
        std::fprintf(stderr, "test_outfilebuf_write_error: %d check(s) failed\n",
                     g_failures);
        return 1;
    }
    std::printf("test_outfilebuf_write_error: PASSED\n");
    return 0;
}
