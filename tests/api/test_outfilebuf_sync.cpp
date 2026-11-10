/*
 * OutFileBuf reports a failed write, with its writer thread (async) and
 * without it (async = false, as the C API runs at nthreads=1): close()
 * throws, with the file closed, and destroying the buffer afterwards, or
 * without closing it, neither throws (a throw out of a destructor ends the
 * process) nor leaves its file open. With the writer thread, a failed write
 * used to leave the next flush() or close() waiting forever.
 *
 * /dev/full fails every write with ENOSPC. The test writes more than stdio
 * buffers, so the failure comes from fwrite and not only from fclose. It
 * skips where there is no /dev/full (macOS) and in WebAssembly.
 */
#include "filebuf.h"
#include "test_helpers.h"
#include <cstdio>
#include <stdexcept>
#include <string>

static int g_failures = 0;

static void check(bool async) {
    const char *mode = async ? "writer thread" : "no writer thread";
    const std::string big(1 << 20, 'x');
    int fds_before = count_open_fds();
    {
        OutFileBuf ob("/dev/full", false, async);
        ob.writeString(big);
        bool threw = false;
        try {
            ob.close();
        } catch (const std::exception &) {
            threw = true;
        }
        CHECK_MSG(threw, "[%s] close() after a failed write did not throw", mode);
        CHECK_MSG(ob.closed(), "[%s] close() threw but left the file open", mode);
    } // destroyed after close() threw: must not throw again
    {
        OutFileBuf ob("/dev/full", false, async);
        ob.writeString(big);
    } // destroyed unclosed: its close() fails, and must not throw
    int fds_after = count_open_fds();
    CHECK_MSG(fds_after == fds_before, "[%s] open file descriptors went from %d "
              "to %d", mode, fds_before, fds_after);
}

int main() {
#ifdef __EMSCRIPTEN__
    std::printf("test_outfilebuf_sync: SKIP (WebAssembly)\n");
    return TEST_SKIP;
#else
    FILE *probe = std::fopen("/dev/full", "w");
    if (probe == NULL) {
        std::printf("test_outfilebuf_sync: SKIP (no /dev/full)\n");
        return TEST_SKIP;
    }
    std::fclose(probe);

    check(false);
    check(true);
    if (g_failures != 0) {
        std::fprintf(stderr, "test_outfilebuf_sync: %d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("test_outfilebuf_sync: PASSED\n");
    return 0;
#endif
}
