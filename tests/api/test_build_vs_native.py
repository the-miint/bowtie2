#!/usr/bin/env python3
"""
PRIMARY CORRECTNESS GATE: Compare API-built index against native bowtie2-build.

1. Build index via api_build helper (uses bt2_build_run)
2. Build index via native bowtie2-build-s
3. Compare all 6 .bt2 files — must be binary identical
4. Align reads against both indexes — results must match
"""

import subprocess
import sys
import os
import filecmp
import tempfile
import shutil

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD_DIR = os.path.join(PROJECT_ROOT, "build")


def find_binary(path):
    """Find a binary, trying the exact path first, then with -debug suffix."""
    if os.path.exists(path):
        return path
    debug_path = path + "-debug"
    if os.path.exists(debug_path):
        return debug_path
    print(f"Binary not found: {path} (also tried {debug_path})", file=sys.stderr)
    sys.exit(1)


API_BUILD = find_binary(os.path.join(BUILD_DIR, "tests", "api", "api_build"))
NATIVE_BUILD = find_binary(os.path.join(BUILD_DIR, "bowtie2-build-s"))
API_DUMP = find_binary(os.path.join(BUILD_DIR, "tests", "api", "api_dump"))
NATIVE_ALIGN = find_binary(os.path.join(BUILD_DIR, "bowtie2-align-s"))

REFERENCE = os.path.join(PROJECT_ROOT, "example", "reference", "lambda_virus.fa")
READS = os.path.join(PROJECT_ROOT, "example", "reads", "longreads.fq")

BT2_SUFFIXES = [".1.bt2", ".2.bt2", ".3.bt2", ".4.bt2", ".rev.1.bt2", ".rev.2.bt2"]


def run_api_build(ref, outbase):
    """Build index via API helper."""
    cmd = [API_BUILD, ref, outbase]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"api_build failed: {result.stderr}", file=sys.stderr)
        sys.exit(1)


def run_native_build(ref, outbase):
    """Build index via native bowtie2-build."""
    cmd = [NATIVE_BUILD, "--quiet", ref, outbase]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"native build failed: {result.stderr}", file=sys.stderr)
        sys.exit(1)


def run_native_align(index, reads):
    """Align reads and return SAM text (no header)."""
    cmd = [NATIVE_ALIGN, "-x", index, "-U", reads, "--quiet"]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"native align failed: {result.stderr}", file=sys.stderr)
        sys.exit(1)
    # Strip header lines
    lines = [l for l in result.stdout.strip().split("\n")
             if l and not l.startswith("@")]
    return lines


def main():
    tmpdir = tempfile.mkdtemp(prefix="bt2_build_test_")
    try:
        api_base = os.path.join(tmpdir, "api_idx")
        native_base = os.path.join(tmpdir, "native_idx")

        # Build both
        print("Building index via API...")
        run_api_build(REFERENCE, api_base)
        print("Building index via native bowtie2-build...")
        run_native_build(REFERENCE, native_base)

        # Compare .bt2 files
        print("Comparing index files...")
        for suffix in BT2_SUFFIXES:
            api_file = api_base + suffix
            native_file = native_base + suffix
            if not os.path.exists(api_file):
                print(f"FAIL: API index missing {suffix}")
                sys.exit(1)
            if not os.path.exists(native_file):
                print(f"FAIL: native index missing {suffix}")
                sys.exit(1)
            if not filecmp.cmp(api_file, native_file, shallow=False):
                # Get sizes for diagnostic
                api_size = os.path.getsize(api_file)
                native_size = os.path.getsize(native_file)
                print(f"FAIL: {suffix} differs (api={api_size} native={native_size})")
                sys.exit(1)

        print(f"PASS [index comparison]: all 6 .bt2 files are binary identical")

        # Align against both indexes and compare
        print("Aligning reads against API-built index...")
        api_sam = run_native_align(api_base, READS)
        print("Aligning reads against native-built index...")
        native_sam = run_native_align(native_base, READS)

        if len(api_sam) != len(native_sam):
            print(f"FAIL [alignment]: record count mismatch: "
                  f"api={len(api_sam)} native={len(native_sam)}")
            sys.exit(1)

        for i, (a, n) in enumerate(zip(api_sam, native_sam)):
            if a != n:
                print(f"FAIL [alignment]: record {i} differs")
                print(f"  api:    {a[:100]}")
                print(f"  native: {n[:100]}")
                sys.exit(1)

        print(f"PASS [alignment comparison]: {len(api_sam)} records match")
        print("test_build_vs_native: ALL PASSED")

    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    main()
