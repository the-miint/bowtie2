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
from concurrent.futures import ThreadPoolExecutor

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


RUNNERS = {
    "native": run_native,
    "api": run_api,
    "api_mem": run_api_memory,
}


def native(reads, reads2=None, flags=None):
    return ("native", reads, reads2, flags)


def api(reads, reads2=None, flags=None):
    return ("api", reads, reads2, flags)


def api_mem(reads, reads2=None, flags=None):
    return ("api_mem", reads, reads2, flags)


def _execute_side(side):
    kind, reads, reads2, flags = side
    return RUNNERS[kind](INDEX, reads, reads2, flags)


# Each case is (section, label, left_side, right_side).
# Cases run in parallel via ThreadPoolExecutor; output is reordered into
# this declared order so PASS lines and banners stay deterministic.
CASES = [
    # --- Cycle 2: Reporting ---
    ("Reporting", "k=3 single-end",
        native(READS_SE, flags=["-k", "3"]),
        api(READS_SE, flags=["--k", "3"])),
    ("Reporting", "-a single-end",
        native(READS_SE, flags=["-a"]),
        api(READS_SE, flags=["--report-all"])),

    # --- Cycle 3: Trimming ---
    ("Trimming", "trim5=10",
        native(READS_SE, flags=["--trim5", "10"]),
        api(READS_SE, flags=["--trim5", "10"])),
    ("Trimming", "trim3=10",
        native(READS_SE, flags=["--trim3", "10"]),
        api(READS_SE, flags=["--trim3", "10"])),

    # --- Cycle 4: Scoring ---
    ("Scoring", "local ma=10",
        native(READS_SE, flags=["--local", "--ma", "10"]),
        api(READS_SE, flags=["--local", "--ma", "10"])),
    ("Scoring", "mp=3",
        native(READS_SE, flags=["--mp", "3"]),
        api(READS_SE, flags=["--mp", "3"])),
    ("Scoring", "np=5",
        native(READS_SE, flags=["--np", "5"]),
        api(READS_SE, flags=["--np", "5"])),
    ("Scoring", "rdg=3,1 rfg=3,1",
        native(READS_SE, flags=["--rdg", "3,1", "--rfg", "3,1"]),
        api(READS_SE, flags=["--rdg-open", "3", "--rdg-extend", "1",
                             "--rfg-open", "3", "--rfg-extend", "1"])),
    ("Scoring", "score-min=L,-1,-1",
        native(READS_SE, flags=["--score-min", "L,-1,-1"]),
        api(READS_SE, flags=["--score-min", "L,-1,-1"])),

    # --- Cycle 5: Paired-end ---
    ("Paired-end", "maxins=100 paired",
        native(READS_1, READS_2, flags=["-X", "100"]),
        api(READS_1, READS_2, flags=["--max-insert", "100"])),
    ("Paired-end", "minins=50 paired",
        native(READS_1, READS_2, flags=["-I", "50"]),
        api(READS_1, READS_2, flags=["--min-insert", "50"])),
    ("Paired-end", "no-discordant paired",
        native(READS_1, READS_2, flags=["--no-discordant"]),
        api(READS_1, READS_2, flags=["--no-discordant"])),
    ("Paired-end", "rf paired",
        native(READS_1, READS_2, flags=["--rf"]),
        api(READS_1, READS_2, flags=["--mate-orient", "RF"])),
    ("Paired-end", "dovetail paired",
        native(READS_1, READS_2, flags=["--dovetail"]),
        api(READS_1, READS_2, flags=["--dovetail"])),
    ("Paired-end", "no-mixed paired",
        native(READS_1, READS_2, flags=["--no-mixed"]),
        api(READS_1, READS_2, flags=["--no-mixed"])),

    # --- Cycle 6: Strand ---
    ("Strand", "norc",
        native(READS_SE, flags=["--norc"]),
        api(READS_SE, flags=["--norc"])),
    ("Strand", "nofw",
        native(READS_SE, flags=["--nofw"]),
        api(READS_SE, flags=["--nofw"])),

    # --- Cycle 7: Effort ---
    ("Effort", "N=1",
        native(READS_SE, flags=["-N", "1"]),
        api(READS_SE, flags=["--seed-mm", "1"])),
    ("Effort", "L=28",
        native(READS_SE, flags=["-L", "28"]),
        api(READS_SE, flags=["--seed-len", "28"])),
    ("Effort", "D=5 R=1",
        native(READS_SE, flags=["-D", "5", "-R", "1"]),
        api(READS_SE, flags=["--max-dp-fail", "5", "--max-seed-rounds", "1"])),

    # --- Cycle 8: SAM output ---
    ("SAM output", "no-unal",
        native(READS_SE, flags=["--no-unal"]),
        api(READS_SE, flags=["--no-unal"])),
    ("SAM output", "xeq",
        native(READS_SE, flags=["--xeq"]),
        api(READS_SE, flags=["--xeq"])),
    ("SAM output", "rg-id=sample42",
        native(READS_SE, flags=["--rg-id", "sample42"]),
        api(READS_SE, flags=["--rg-id", "sample42"])),

    # --- Cycle 9: Other ---
    ("Other", "ignore-quals",
        native(READS_SE, flags=["--ignore-quals"]),
        api(READS_SE, flags=["--ignore-quals"])),
    ("Other", "reorder",
        native(READS_SE, flags=["--reorder"]),
        api(READS_SE, flags=["--reorder"])),

    # --- Cycle 10: Combos ---
    ("Combos", "combo: local+scoring+k+trim",
        native(READS_SE, flags=["--local", "--ma", "4", "--mp", "3", "-k", "3", "--trim5", "5"]),
        api(READS_SE, flags=["--local", "--ma", "4", "--mp", "3", "--k", "3", "--trim5", "5"])),
    ("Combos", "combo: paired strict constraints",
        native(READS_1, READS_2, flags=["-X", "200", "--no-discordant", "--no-mixed", "-N", "1"]),
        api(READS_1, READS_2, flags=["--max-insert", "200", "--no-discordant", "--no-mixed", "--seed-mm", "1"])),

    # --- Cycle 11: Memory path parity (api file vs api memory) ---
    ("Memory path", "memory vs file: local+ma+trim+k",
        api(READS_SE, flags=["--local", "--ma", "4", "--trim5", "5", "--k", "3"]),
        api_mem(READS_SE, flags=["--local", "--ma", "4", "--trim5", "5", "--k", "3"])),
    ("Memory path", "memory vs file: norc+ignore-quals",
        api(READS_SE, flags=["--norc", "--ignore-quals"]),
        api_mem(READS_SE, flags=["--norc", "--ignore-quals"])),
    ("Memory path", "memory vs file: xeq+N=1",
        api(READS_SE, flags=["--xeq", "--seed-mm", "1"]),
        api_mem(READS_SE, flags=["--xeq", "--seed-mm", "1"])),
]


SUBSET_LABELS = frozenset({
    "-a single-end",
    "local ma=10",
    "maxins=100 paired",
    "memory vs file: xeq+N=1",
})


def main():
    # Cap workers to keep memory pressure manageable under ASan/TSan, where
    # each bowtie2 process can use several hundred MB. 4 saturates the 2-4
    # core GitHub runners without OOMing on sanitizer jobs.
    # TSan needs an extra ceiling: shadow memory is ~8x heap, so 4 parallel
    # instrumented processes thrash the 4 GiB runner. Drop to 2 there.
    cpu_cap = min(4, (os.cpu_count() or 2))
    workers = 2 if "TSAN_OPTIONS" in os.environ else cpu_cap

    # BT2_FAST_SUBSET runs a 4-case smoke set covering reporting, scoring,
    # paired-end, and memory-path translation. Used by the TSan CI job
    # where the full 30-case sweep is prohibitively slow and the threading
    # surface exercised here is already covered by test_align_basic/paired.
    if os.environ.get("BT2_FAST_SUBSET"):
        cases = [c for c in CASES if c[1] in SUBSET_LABELS]
        missing = SUBSET_LABELS - {c[1] for c in cases}
        if missing:
            print(f"BT2_FAST_SUBSET: missing labels {sorted(missing)}", file=sys.stderr)
            sys.exit(1)
    else:
        cases = CASES

    def execute_case(idx_case):
        idx, (section, label, left, right) = idx_case
        return idx, section, label, _execute_side(left), _execute_side(right)

    with ThreadPoolExecutor(max_workers=workers) as ex:
        results = list(ex.map(execute_case, enumerate(cases)))

    current_section = None
    for _, section, label, left, right in results:
        if section != current_section:
            print(f"=== {section} ===")
            current_section = section
        compare_records(left, right, label)

    print("\ntest_options_vs_native: ALL PASSED")


if __name__ == "__main__":
    main()
