#!/usr/bin/env python3
"""
PRIMARY CORRECTNESS GATE: Compare in-memory API (bt2_align_run) output
against file-based API (bt2_align_run_files) output.

Runs api_dump (file-based) and api_dump_memory (in-memory) on the same
input and compares every field record-by-record. They must be identical.
"""

import subprocess
import sys
import os

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


API_DUMP = find_binary(os.path.join(BUILD_DIR, "tests", "api", "api_dump"))
API_DUMP_MEMORY = find_binary(os.path.join(BUILD_DIR, "tests", "api", "api_dump_memory"))
INDEX = os.path.join(PROJECT_ROOT, "example", "index", "lambda_virus")


def parse_tsv(text):
    """Parse api_dump / api_dump_memory TSV output into list of dicts."""
    records = []
    for line in text.strip().split("\n"):
        if not line:
            continue
        fields = line.split("\t")
        if len(fields) < 15:
            continue
        rec = {
            "QNAME": fields[0],
            "FLAG": int(fields[1]),
            "RNAME": fields[2],
            "POS": int(fields[3]),
            "MAPQ": int(fields[4]),
            "CIGAR": fields[5],
            "RNEXT": fields[6],
            "PNEXT": int(fields[7]),
            "TLEN": int(fields[8]),
            "SEQ": fields[9],
            "QUAL": fields[10],
            "AS": int(fields[11]),
            "NM": int(fields[12]),
            "MD": fields[13] if fields[13] else None,
            "YT": fields[14] if fields[14] else None,
        }
        records.append(rec)
    return records


def run_file_api(index, reads, reads2=None):
    """Run api_dump (file-based) and return parsed records."""
    cmd = [API_DUMP, index, reads]
    if reads2:
        cmd.append(reads2)
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"api_dump failed: {result.stderr}", file=sys.stderr)
        sys.exit(1)
    return parse_tsv(result.stdout)


def run_memory_api(index, reads, reads2=None):
    """Run api_dump_memory (in-memory) and return parsed records."""
    cmd = [API_DUMP_MEMORY, index, reads]
    if reads2:
        cmd.append(reads2)
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"api_dump_memory failed: {result.stderr}", file=sys.stderr)
        sys.exit(1)
    return parse_tsv(result.stdout)


def compare_records(file_recs, mem_recs, label):
    """Compare two lists of records field-by-field."""
    if len(file_recs) != len(mem_recs):
        print(f"FAIL [{label}]: record count mismatch: "
              f"file={len(file_recs)} memory={len(mem_recs)}")
        sys.exit(1)

    fields = ["QNAME", "FLAG", "RNAME", "POS", "MAPQ", "CIGAR",
              "RNEXT", "PNEXT", "TLEN", "SEQ", "QUAL",
              "AS", "NM", "MD", "YT"]

    for i in range(len(file_recs)):
        for field in fields:
            fval = file_recs[i].get(field)
            mval = mem_recs[i].get(field)
            if fval != mval:
                print(f"FAIL [{label}] record {i} field {field}: "
                      f"file={fval!r} memory={mval!r}")
                sys.exit(1)

    print(f"PASS [{label}]: {len(file_recs)} records match")


def main():
    reads_se = os.path.join(PROJECT_ROOT, "example", "reads", "longreads.fq")
    reads_1 = os.path.join(PROJECT_ROOT, "example", "reads", "reads_1.fq")
    reads_2 = os.path.join(PROJECT_ROOT, "example", "reads", "reads_2.fq")

    # Single-end: in-memory vs file
    print("Running single-end: memory vs file comparison...")
    file_se = run_file_api(INDEX, reads_se)
    mem_se = run_memory_api(INDEX, reads_se)
    compare_records(file_se, mem_se, "single-end memory-vs-file")

    # Paired-end: in-memory vs file
    print("Running paired-end: memory vs file comparison...")
    file_pe = run_file_api(INDEX, reads_1, reads_2)
    mem_pe = run_memory_api(INDEX, reads_1, reads_2)
    compare_records(file_pe, mem_pe, "paired-end memory-vs-file")

    print("test_input_memory_vs_file: ALL PASSED")


if __name__ == "__main__":
    main()
