/*
 * Shared test helpers for API tests.
 */
#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include "bt2_api.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>

/* Create a truncated/invalid index file set at the given base path.
   Writes 4 junk bytes to each of the 6 .bt2 files — enough for
   index_files_exist() to succeed but Ebwt loading to fail. */
static inline void create_truncated_index(const char *base) {
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

static inline void remove_truncated_index(const char *base) {
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
static inline void cleanup_index(const char *base) {
    remove_truncated_index(base);  /* same suffixes */
}

static inline int file_exists(const char *path) {
    FILE *f = fopen(path, "r");
    if (f) { fclose(f); return 1; }
    return 0;
}

/* Exit code of a test that cannot run here; ctest reports it as skipped
   (SKIP_RETURN_CODE in tests/api/CMakeLists.txt), not as passed. */
#define TEST_SKIP 77

/* Whether this build can start threads: a WebAssembly build without
   pthreads cannot, and aligns at nthreads=1 only. */
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
#define TEST_HAS_THREADS 0
#else
#define TEST_HAS_THREADS 1
#endif

/* Entries in a directory, not counting names that start with '.', or -1 if
   it can't be listed. */
static inline int count_dir_entries(const char *path) {
    DIR *d = opendir(path);
    if (d == NULL) return -1;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] != '.') n++;
    }
    closedir(d);
    return n;
}

/* Open file descriptors in this process (/dev/fd), or -1 where they can't
   be counted: in WebAssembly the /dev/fd the module can open is the host's,
   and elsewhere the count is used only once opening a file is seen to add
   one to it. */
static inline int count_open_fds(void) {
#ifdef __EMSCRIPTEN__
    return -1;
#else
    static int trusted = -1; /* whether opening a file adds one; -1 untried */
    if (trusted < 0) {
        int before = count_dir_entries("/dev/fd");
        FILE *f = fopen("/dev/null", "r");
        int during = f != NULL ? count_dir_entries("/dev/fd") : -1;
        if (f != NULL) fclose(f);
        trusted = before >= 0 && during == before + 1;
    }
    return trusted ? count_dir_entries("/dev/fd") : -1;
#endif
}

/* Threads in this process (/proc/self/task, Linux), or -1 where not
   counted. */
static inline int count_threads(void) {
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    return count_dir_entries("/proc/self/task");
#else
    return -1;
#endif
}

/* The SAM flag of a record that did not align. */
#define SAM_FLAG_UNMAPPED 0x4

/* Whether out has a mapped record for the read named name. */
static inline int aligned_by_name(const bt2_align_output_t *out, const char *name) {
    for (size_t i = 0; out != NULL && i < out->n_records; i++) {
        if (strcmp(out->qname[i], name) == 0 &&
            !(out->flag[i] & SAM_FLAG_UNMAPPED)) return 1;
    }
    return 0;
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
