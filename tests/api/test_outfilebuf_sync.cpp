/*
 * OutFileBuf without a writer thread (async = false), as the C API runs it
 * at nthreads=1: a write that fails is reported by close(), and destroying
 * the buffer afterwards, or without closing it, does not throw (a throw out
 * of a destructor ends the process).
 *
 * /dev/full fails every write with ENOSPC. The test writes more than stdio
 * buffers, so the failure comes from fwrite and not only from fclose. It
 * skips where there is no /dev/full (macOS) and in WebAssembly.
 */
#include "filebuf.h"
#include <cstdio>
#include <stdexcept>
#include <string>

int main() {
#ifdef __EMSCRIPTEN__
    std::printf("test_outfilebuf_sync: SKIP (WebAssembly)\n");
    return 0;
#else
    FILE *probe = std::fopen("/dev/full", "w");
    if (probe == NULL) {
        std::printf("test_outfilebuf_sync: SKIP (no /dev/full)\n");
        return 0;
    }
    std::fclose(probe);

    const std::string big(1 << 20, 'x');
    int failures = 0;
    {
        OutFileBuf ob("/dev/full", false, false);
        ob.writeString(big);
        bool threw = false;
        try {
            ob.close();
        } catch (const std::exception &) {
            threw = true;
        }
        if (!threw) {
            std::fprintf(stderr, "FAIL: close() after a failed write did not throw\n");
            failures++;
        }
    } // destroyed after close() threw: must not throw again
    {
        OutFileBuf ob("/dev/full", false, false);
        ob.writeString(big);
    } // destroyed unclosed: its close() fails, and must not throw

    if (failures != 0) return 1;
    std::printf("test_outfilebuf_sync: PASSED\n");
    return 0;
#endif
}
