#!/usr/bin/env python3
"""
CORRECTNESS GATE: Compare API output with config options against native bowtie2.

For each option (or option combination), runs native bowtie2 CLI and the API
(via api_dump helper) with identical flags and compares every SAM field
record-by-record.
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


NATIVE_BT2 = find_binary(os.path.join(BUILD_DIR, "bowtie2-align-s"))
API_DUMP = find_binary(os.path.join(BUILD_DIR, "tests", "api", "api_dump"))
API_DUMP_MEMORY = find_binary(os.path.join(BUILD_DIR, "tests", "api", "api_dump_memory"))
INDEX = os.path.join(PROJECT_ROOT, "example", "index", "lambda_virus")
READS_SE = os.path.join(PROJECT_ROOT, "example", "reads", "longreads.fq")
READS_1 = os.path.join(PROJECT_ROOT, "example", "reads", "reads_1.fq")
READS_2 = os.path.join(PROJECT_ROOT, "example", "reads", "reads_2.fq")


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


def run_native(index, reads, reads2=None, extra_flags=None):
    """Run native bowtie2 and return parsed records."""
    cmd = [NATIVE_BT2, "-x", index, "--quiet"]
    if extra_flags:
        cmd += extra_flags
    if reads2:
        cmd += ["-1", reads, "-2", reads2]
    else:
        cmd += ["-U", reads]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"Native bowtie2 failed: {result.stderr}", file=sys.stderr)
        sys.exit(1)
    return parse_sam(result.stdout)


def run_api(index, reads, reads2=None, extra_flags=None):
    """Run api_dump helper and return parsed records."""
    cmd = [API_DUMP, index, reads]
    if reads2:
        cmd.append(reads2)
    if extra_flags:
        cmd += extra_flags
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"api_dump failed: {result.stderr}", file=sys.stderr)
        sys.exit(1)
    return parse_api_dump(result.stdout)


def run_api_memory(index, reads, reads2=None, extra_flags=None):
    """Run api_dump_memory helper (in-memory path) and return parsed records."""
    cmd = [API_DUMP_MEMORY, index, reads]
    if reads2:
        cmd.append(reads2)
    if extra_flags:
        cmd += extra_flags
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"api_dump_memory failed: {result.stderr}", file=sys.stderr)
        sys.exit(1)
    return parse_api_dump(result.stdout)


def compare_records(native, api, label):
    """Compare two lists of records field-by-field."""
    if len(native) != len(api):
        print(f"FAIL [{label}]: record count mismatch: native={len(native)} api={len(api)}")
        sys.exit(1)

    fields_to_compare = ["QNAME", "FLAG", "RNAME", "POS", "MAPQ", "CIGAR",
                         "RNEXT", "PNEXT", "TLEN"]
    seq_fields = ["SEQ", "QUAL"]
    tag_fields = ["AS", "NM", "MD", "YT"]

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
            if nval is None:
                continue
            if nval != aval:
                print(f"FAIL [{label}] record {i} tag {field}: "
                      f"native={nval!r} api={aval!r}")
                sys.exit(1)

    print(f"PASS [{label}]: {len(native)} records match")


def main():
    # --- Cycle 2: Reporting ---
    print("=== Reporting ===")

    native = run_native(INDEX, READS_SE, extra_flags=["-k", "3"])
    api = run_api(INDEX, READS_SE, extra_flags=["--k", "3"])
    compare_records(native, api, "k=3 single-end")

    native = run_native(INDEX, READS_SE, extra_flags=["-a"])
    api = run_api(INDEX, READS_SE, extra_flags=["--report-all"])
    compare_records(native, api, "-a single-end")

    # --- Cycle 3: Trimming ---
    print("=== Trimming ===")

    native = run_native(INDEX, READS_SE, extra_flags=["--trim5", "10"])
    api = run_api(INDEX, READS_SE, extra_flags=["--trim5", "10"])
    compare_records(native, api, "trim5=10")

    native = run_native(INDEX, READS_SE, extra_flags=["--trim3", "10"])
    api = run_api(INDEX, READS_SE, extra_flags=["--trim3", "10"])
    compare_records(native, api, "trim3=10")

    # --- Cycle 4: Scoring ---
    print("=== Scoring ===")

    native = run_native(INDEX, READS_SE, extra_flags=["--local", "--ma", "10"])
    api = run_api(INDEX, READS_SE, extra_flags=["--local", "--ma", "10"])
    compare_records(native, api, "local ma=10")

    native = run_native(INDEX, READS_SE, extra_flags=["--mp", "3"])
    api = run_api(INDEX, READS_SE, extra_flags=["--mp", "3"])
    compare_records(native, api, "mp=3")

    native = run_native(INDEX, READS_SE, extra_flags=["--np", "5"])
    api = run_api(INDEX, READS_SE, extra_flags=["--np", "5"])
    compare_records(native, api, "np=5")

    native = run_native(INDEX, READS_SE, extra_flags=["--rdg", "3,1", "--rfg", "3,1"])
    api = run_api(INDEX, READS_SE, extra_flags=["--rdg-open", "3", "--rdg-extend", "1",
                                                 "--rfg-open", "3", "--rfg-extend", "1"])
    compare_records(native, api, "rdg=3,1 rfg=3,1")

    native = run_native(INDEX, READS_SE, extra_flags=["--score-min", "L,-1,-1"])
    api = run_api(INDEX, READS_SE, extra_flags=["--score-min", "L,-1,-1"])
    compare_records(native, api, "score-min=L,-1,-1")

    # --- Cycle 5: Paired-end ---
    print("=== Paired-end ===")

    native = run_native(INDEX, READS_1, READS_2, extra_flags=["-X", "100"])
    api = run_api(INDEX, READS_1, READS_2, extra_flags=["--max-insert", "100"])
    compare_records(native, api, "maxins=100 paired")

    native = run_native(INDEX, READS_1, READS_2, extra_flags=["-I", "50"])
    api = run_api(INDEX, READS_1, READS_2, extra_flags=["--min-insert", "50"])
    compare_records(native, api, "minins=50 paired")

    native = run_native(INDEX, READS_1, READS_2, extra_flags=["--no-discordant"])
    api = run_api(INDEX, READS_1, READS_2, extra_flags=["--no-discordant"])
    compare_records(native, api, "no-discordant paired")

    native = run_native(INDEX, READS_1, READS_2, extra_flags=["--rf"])
    api = run_api(INDEX, READS_1, READS_2, extra_flags=["--mate-orient", "RF"])
    compare_records(native, api, "rf paired")

    native = run_native(INDEX, READS_1, READS_2, extra_flags=["--dovetail"])
    api = run_api(INDEX, READS_1, READS_2, extra_flags=["--dovetail"])
    compare_records(native, api, "dovetail paired")

    native = run_native(INDEX, READS_1, READS_2, extra_flags=["--no-mixed"])
    api = run_api(INDEX, READS_1, READS_2, extra_flags=["--no-mixed"])
    compare_records(native, api, "no-mixed paired")

    # --- Cycle 6: Strand ---
    print("=== Strand ===")

    native = run_native(INDEX, READS_SE, extra_flags=["--norc"])
    api = run_api(INDEX, READS_SE, extra_flags=["--norc"])
    compare_records(native, api, "norc")

    native = run_native(INDEX, READS_SE, extra_flags=["--nofw"])
    api = run_api(INDEX, READS_SE, extra_flags=["--nofw"])
    compare_records(native, api, "nofw")

    # --- Cycle 7: Effort ---
    print("=== Effort ===")

    native = run_native(INDEX, READS_SE, extra_flags=["-N", "1"])
    api = run_api(INDEX, READS_SE, extra_flags=["--seed-mm", "1"])
    compare_records(native, api, "N=1")

    native = run_native(INDEX, READS_SE, extra_flags=["-L", "28"])
    api = run_api(INDEX, READS_SE, extra_flags=["--seed-len", "28"])
    compare_records(native, api, "L=28")

    native = run_native(INDEX, READS_SE, extra_flags=["-D", "5", "-R", "1"])
    api = run_api(INDEX, READS_SE, extra_flags=["--max-dp-fail", "5", "--max-seed-rounds", "1"])
    compare_records(native, api, "D=5 R=1")

    # --- Cycle 8: SAM output ---
    print("=== SAM output ===")

    native = run_native(INDEX, READS_SE, extra_flags=["--no-unal"])
    api = run_api(INDEX, READS_SE, extra_flags=["--no-unal"])
    compare_records(native, api, "no-unal")

    native = run_native(INDEX, READS_SE, extra_flags=["--xeq"])
    api = run_api(INDEX, READS_SE, extra_flags=["--xeq"])
    compare_records(native, api, "xeq")

    native = run_native(INDEX, READS_SE, extra_flags=["--rg-id", "sample42"])
    api = run_api(INDEX, READS_SE, extra_flags=["--rg-id", "sample42"])
    compare_records(native, api, "rg-id=sample42")

    # --- Cycle 9: Other ---
    print("=== Other ===")

    native = run_native(INDEX, READS_SE, extra_flags=["--ignore-quals"])
    api = run_api(INDEX, READS_SE, extra_flags=["--ignore-quals"])
    compare_records(native, api, "ignore-quals")

    native = run_native(INDEX, READS_SE, extra_flags=["--reorder"])
    api = run_api(INDEX, READS_SE, extra_flags=["--reorder"])
    compare_records(native, api, "reorder")

    # --- Cycle 10: Combos ---
    print("=== Combos ===")

    flags_n = ["--local", "--ma", "4", "--mp", "3", "-k", "3", "--trim5", "5"]
    flags_a = ["--local", "--ma", "4", "--mp", "3", "--k", "3", "--trim5", "5"]
    native = run_native(INDEX, READS_SE, extra_flags=flags_n)
    api = run_api(INDEX, READS_SE, extra_flags=flags_a)
    compare_records(native, api, "combo: local+scoring+k+trim")

    flags_n = ["-X", "200", "--no-discordant", "--no-mixed", "-N", "1"]
    flags_a = ["--max-insert", "200", "--no-discordant", "--no-mixed", "--seed-mm", "1"]
    native = run_native(INDEX, READS_1, READS_2, extra_flags=flags_n)
    api = run_api(INDEX, READS_1, READS_2, extra_flags=flags_a)
    compare_records(native, api, "combo: paired strict constraints")

    # --- Cycle 11: Memory path parity ---
    print("=== Memory path ===")

    # Memory path should produce identical output to file path with same options
    flags = ["--local", "--ma", "4", "--trim5", "5", "--k", "3"]
    file_out = run_api(INDEX, READS_SE, extra_flags=flags)
    mem_out = run_api_memory(INDEX, READS_SE, extra_flags=flags)
    compare_records(file_out, mem_out, "memory vs file: local+ma+trim+k")

    flags = ["--norc", "--ignore-quals"]
    file_out = run_api(INDEX, READS_SE, extra_flags=flags)
    mem_out = run_api_memory(INDEX, READS_SE, extra_flags=flags)
    compare_records(file_out, mem_out, "memory vs file: norc+ignore-quals")

    flags = ["--xeq", "--seed-mm", "1"]
    file_out = run_api(INDEX, READS_SE, extra_flags=flags)
    mem_out = run_api_memory(INDEX, READS_SE, extra_flags=flags)
    compare_records(file_out, mem_out, "memory vs file: xeq+N=1")

    print("\ntest_options_vs_native: ALL PASSED")


if __name__ == "__main__":
    main()
