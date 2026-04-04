#!/usr/bin/env python3
"""
PRIMARY CORRECTNESS GATE: Compare API output against native bowtie2.

Runs native bowtie2 and the API (via api_dump helper) on the same inputs
and compares every SAM field record-by-record.
"""

import subprocess
import sys
import os

# Paths relative to project root (tests run from build/ or project root)
PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD_DIR = os.path.join(PROJECT_ROOT, "build")


def find_binary(path):
    """Find a binary, trying the exact path first, then with -debug suffix."""
    if os.path.exists(path):
        return path
    # Debug/sanitizer builds append -debug to binary names
    debug_path = path + "-debug"
    if os.path.exists(debug_path):
        return debug_path
    print(f"Binary not found: {path} (also tried {debug_path})", file=sys.stderr)
    sys.exit(1)


NATIVE_BT2 = find_binary(os.path.join(BUILD_DIR, "bowtie2-align-s"))
API_DUMP = find_binary(os.path.join(BUILD_DIR, "tests", "api", "api_dump"))
INDEX = os.path.join(PROJECT_ROOT, "example", "index", "lambda_virus")


def parse_sam(text):
    """Parse SAM text into list of dicts (skip headers)."""
    records = []
    for line in text.strip().split("\n"):
        if not line or line.startswith("@"):
            continue
        fields = line.split("\t")
        if len(fields) < 11:
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
        }
        # Parse optional tags
        for f in fields[11:]:
            if f.startswith("AS:i:"):
                rec["AS"] = int(f[5:])
            elif f.startswith("NM:i:"):
                rec["NM"] = int(f[5:])
            elif f.startswith("MD:Z:"):
                rec["MD"] = f[5:]
            elif f.startswith("YT:Z:"):
                rec["YT"] = f[5:]
        records.append(rec)
    return records


def parse_api_dump(text):
    """Parse api_dump TSV output into list of dicts."""
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


def run_native(index, reads, reads2=None):
    """Run native bowtie2 and return parsed records."""
    cmd = [NATIVE_BT2, "-x", index, "--quiet"]
    if reads2:
        cmd += ["-1", reads, "-2", reads2]
    else:
        cmd += ["-U", reads]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"Native bowtie2 failed: {result.stderr}", file=sys.stderr)
        sys.exit(1)
    return parse_sam(result.stdout)


def run_api(index, reads, reads2=None):
    """Run api_dump helper and return parsed records."""
    cmd = [API_DUMP, index, reads]
    if reads2:
        cmd.append(reads2)
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"api_dump failed: {result.stderr}", file=sys.stderr)
        sys.exit(1)
    return parse_api_dump(result.stdout)


def compare_records(native, api, label):
    """Compare two lists of records field-by-field."""
    if len(native) != len(api):
        print(f"FAIL [{label}]: record count mismatch: native={len(native)} api={len(api)}")
        sys.exit(1)

    fields_to_compare = ["QNAME", "FLAG", "RNAME", "POS", "MAPQ", "CIGAR",
                         "RNEXT", "PNEXT", "TLEN"]
    # SEQ/QUAL: API always emits full data even for secondary alignments,
    # while native SAM may output "*" when --omit-sec-seq is set.
    # Compare SEQ/QUAL only when native has actual data (not "*").
    seq_fields = ["SEQ", "QUAL"]
    tag_fields = ["AS", "NM", "MD", "YT"]

    XS_ABSENT = -2147483648  # INT32_MIN sentinel for absent XS tag

    for i in range(len(native)):
        for field in fields_to_compare:
            if native[i][field] != api[i][field]:
                print(f"FAIL [{label}] record {i} field {field}: "
                      f"native={native[i][field]!r} api={api[i][field]!r}")
                sys.exit(1)
        for field in seq_fields:
            nval = native[i][field]
            aval = api[i][field]
            if nval != "*" and nval != aval:
                print(f"FAIL [{label}] record {i} field {field}: "
                      f"native={nval!r} api={aval!r}")
                sys.exit(1)
        for field in tag_fields:
            nval = native[i].get(field)
            aval = api[i].get(field)
            # Skip tags absent from native output
            if nval is None:
                continue
            # XS: API uses INT32_MIN sentinel for absent; skip if sentinel
            if field == "XS" and aval == XS_ABSENT:
                continue
            if nval != aval:
                print(f"FAIL [{label}] record {i} tag {field}: "
                      f"native={nval!r} api={aval!r}")
                sys.exit(1)

    print(f"PASS [{label}]: {len(native)} records match")


def main():
    reads_se = os.path.join(PROJECT_ROOT, "example", "reads", "longreads.fq")
    reads_1 = os.path.join(PROJECT_ROOT, "example", "reads", "reads_1.fq")
    reads_2 = os.path.join(PROJECT_ROOT, "example", "reads", "reads_2.fq")

    # Single-end comparison
    print("Running single-end comparison...")
    native_se = run_native(INDEX, reads_se)
    api_se = run_api(INDEX, reads_se)
    compare_records(native_se, api_se, "single-end")

    # Paired-end comparison
    print("Running paired-end comparison...")
    native_pe = run_native(INDEX, reads_1, reads_2)
    api_pe = run_api(INDEX, reads_1, reads_2)
    compare_records(native_pe, api_pe, "paired-end")

    print("test_align_vs_native: ALL PASSED")


if __name__ == "__main__":
    main()
