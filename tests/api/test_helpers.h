/*
 * Shared test helpers for API tests.
 */
#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <stdio.h>
#include <string.h>

/* Create a truncated/invalid index file set at the given base path.
   Writes 4 junk bytes to each of the 6 .bt2 files — enough for
   index_files_exist() to succeed but Ebwt loading to fail. */
static void create_truncated_index(const char *base) {
    char path[512];
    const char *suffixes[] = {
        ".1.bt2", ".2.bt2", ".3.bt2", ".4.bt2", ".rev.1.bt2", ".rev.2.bt2"
    };
    int i;
    for (i = 0; i < 6; i++) {
        snprintf(path, sizeof(path), "%s%s", base, suffixes[i]);
        FILE *f = fopen(path, "wb");
        if (!f) continue;
        unsigned char junk[] = {0xDE, 0xAD, 0xBE, 0xEF};
        fwrite(junk, 1, sizeof(junk), f);
        fclose(f);
    }
}

static void remove_truncated_index(const char *base) {
    char path[512];
    const char *suffixes[] = {
        ".1.bt2", ".2.bt2", ".3.bt2", ".4.bt2", ".rev.1.bt2", ".rev.2.bt2"
    };
    int i;
    for (i = 0; i < 6; i++) {
        snprintf(path, sizeof(path), "%s%s", base, suffixes[i]);
        remove(path);
    }
}

/* Remove all 6 .bt2 index files at the given base path. */
static void cleanup_index(const char *base) {
    remove_truncated_index(base);  /* same suffixes */
}

static int file_exists(const char *path) {
    FILE *f = fopen(path, "r");
    if (f) { fclose(f); return 1; }
    return 0;
}

#ifndef __EMSCRIPTEN__
#include <dirent.h>
#endif

/* Open file descriptors in this process (/dev/fd, on Linux and macOS), or -1
   where they can't be counted: no /dev/fd, or WebAssembly, where the
   /dev/fd the module can open is the host's. */
static int count_open_fds(void) {
#ifdef __EMSCRIPTEN__
    return -1;
#else
    DIR *d = opendir("/dev/fd");
    if (d == NULL) return -1;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] != '.') n++;
    }
    closedir(d);
    return n;
#endif
}

/* A failed check: print the printf-style message and count it in the
   test's own `static int g_failures`. */
#define CHECK_MSG(cond, ...) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: " __VA_ARGS__); \
        fprintf(stderr, "\n"); \
        g_failures++; \
    } \
} while (0)

#endif /* TEST_HELPERS_H */
