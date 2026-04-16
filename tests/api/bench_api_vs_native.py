#!/usr/bin/env python3
"""
Performance benchmark: API vs native bowtie2.

Compares wall-clock time for:
  1. Native bowtie2 binary (subprocess)
  2. API file-based path (bt2_align_run_files)
  3. API in-memory path (bt2_align_run)

Generates a larger synthetic dataset by replicating example reads,
then runs each approach multiple times to get stable timings.
"""

import json
import os
import subprocess
import sys
import tempfile
import time

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD_DIR = os.path.join(PROJECT_ROOT, "build")

ITERATIONS = 5


def find_binary(path):
    if os.path.exists(path):
        return path
    debug_path = path + "-debug"
    if os.path.exists(debug_path):
        return debug_path
    print(f"Binary not found: {path}", file=sys.stderr)
    sys.exit(1)


NATIVE_BT2 = find_binary(os.path.join(BUILD_DIR, "bowtie2-align-s"))
BENCH_API = find_binary(os.path.join(BUILD_DIR, "tests", "api", "bench_api"))
INDEX = os.path.join(PROJECT_ROOT, "example", "index", "lambda_virus")


def replicate_fastq(src_path, dest_path, factor):
    """Replicate a FASTQ file `factor` times with unique read names."""
    records = []
    with open(src_path) as f:
        lines = f.readlines()
    for i in range(0, len(lines), 4):
        if i + 3 < len(lines):
            records.append(lines[i:i+4])

    with open(dest_path, 'w') as out:
        for rep in range(factor):
            for rec in records:
                # Mangle read name to keep unique
                name_line = rec[0]
                if name_line.startswith('@'):
                    name_line = f"@rep{rep}_{name_line[1:]}"
                out.write(name_line)
                out.write(rec[1])
                out.write(rec[2])
                out.write(rec[3])

    return len(records) * factor


def time_native(index, reads1, reads2=None, iterations=ITERATIONS):
    """Time native bowtie2 binary over multiple runs."""
    times = []
    for _ in range(iterations):
        cmd = [NATIVE_BT2, "-x", index, "--quiet", "-S", "/dev/null"]
        if reads2:
            cmd += ["-1", reads1, "-2", reads2]
        else:
            cmd += ["-U", reads1]
        t0 = time.perf_counter()
        result = subprocess.run(cmd, capture_output=True, text=True)
        elapsed = (time.perf_counter() - t0) * 1000  # ms
        if result.returncode != 0:
            print(f"Native failed: {result.stderr}", file=sys.stderr)
            sys.exit(1)
        times.append(elapsed)
    return times


def time_api(bench_binary, mode, index, reads1, reads2=None, iterations=ITERATIONS):
    """Time API via bench_api helper. Returns parsed JSON results."""
    cmd = [bench_binary, mode, index, reads1]
    if reads2:
        cmd.append(reads2)
    cmd.append(str(iterations))
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"bench_api ({mode}) failed: {result.stderr}", file=sys.stderr)
        sys.exit(1)
    return json.loads(result.stdout.strip())


def format_table(title, native_times, api_file_result, api_mem_result, n_reads):
    """Format benchmark results as a table."""
    native_avg = sum(native_times) / len(native_times)
    native_min = min(native_times)
    api_file_avg = api_file_result["avg_align_ms"]
    api_mem_avg = api_mem_result["avg_align_ms"]

    print(f"\n{'='*65}")
    print(f"  {title}  ({n_reads:,} reads, {ITERATIONS} iterations)")
    print(f"{'='*65}")
    print(f"  {'Method':<25} {'Avg (ms)':>10} {'vs Native':>12} {'Reads/sec':>12}")
    print(f"  {'-'*25} {'-'*10} {'-'*12} {'-'*12}")

    def row(label, avg_ms, baseline_ms):
        ratio = avg_ms / baseline_ms if baseline_ms > 0 else 0
        reads_per_sec = n_reads / (avg_ms / 1000) if avg_ms > 0 else 0
        speedup = f"{ratio:.2f}x"
        print(f"  {label:<25} {avg_ms:>10.1f} {speedup:>12} {reads_per_sec:>12,.0f}")

    row("Native bowtie2", native_avg, native_avg)
    row("API (file-based)", api_file_avg, native_avg)
    row("API (in-memory)", api_mem_avg, native_avg)

    print()
    print(f"  Context creation: {api_file_result['create_ms']:.1f} ms (file), "
          f"{api_mem_result['create_ms']:.1f} ms (memory)")
    print(f"  Native min: {native_min:.1f} ms")
    print()


def main():
    # Determine replication factor from command line or default
    replicate = int(sys.argv[1]) if len(sys.argv) > 1 else 10

    reads_se = os.path.join(PROJECT_ROOT, "example", "reads", "longreads.fq")
    reads_1 = os.path.join(PROJECT_ROOT, "example", "reads", "reads_1.fq")
    reads_2 = os.path.join(PROJECT_ROOT, "example", "reads", "reads_2.fq")

    with tempfile.TemporaryDirectory() as tmpdir:
        # Generate replicated datasets
        big_se = os.path.join(tmpdir, "big_se.fq")
        big_pe1 = os.path.join(tmpdir, "big_pe1.fq")
        big_pe2 = os.path.join(tmpdir, "big_pe2.fq")

        print(f"Generating {replicate}x replicated datasets...")
        n_se = replicate_fastq(reads_se, big_se, replicate)
        n_pe = replicate_fastq(reads_1, big_pe1, replicate)
        replicate_fastq(reads_2, big_pe2, replicate)

        print(f"  Single-end: {n_se:,} reads")
        print(f"  Paired-end: {n_pe:,} x 2 reads")

        # --- Single-end benchmark ---
        print(f"\nRunning single-end benchmark ({ITERATIONS} iterations)...")
        native_se_times = time_native(INDEX, big_se)
        api_file_se = time_api(BENCH_API, "file", INDEX, big_se)
        api_mem_se = time_api(BENCH_API, "memory", INDEX, big_se)
        format_table("Single-end Alignment", native_se_times, api_file_se, api_mem_se, n_se)

        # --- Paired-end benchmark ---
        print(f"Running paired-end benchmark ({ITERATIONS} iterations)...")
        native_pe_times = time_native(INDEX, big_pe1, big_pe2)
        api_file_pe = time_api(BENCH_API, "file", INDEX, big_pe1, big_pe2)
        api_mem_pe = time_api(BENCH_API, "memory", INDEX, big_pe1, big_pe2)
        format_table("Paired-end Alignment", native_pe_times, api_file_pe, api_mem_pe, n_pe)

        # --- Context reuse benchmark ---
        print("Context reuse advantage (amortized creation):")
        print(f"  The API creates the context ONCE and reuses it across {ITERATIONS} runs.")
        print(f"  Native bowtie2 loads the index from scratch each invocation.")
        native_total = sum(native_se_times)
        api_mem_total = api_mem_se["total_align_ms"] + api_mem_se["create_ms"]
        print(f"  Native total ({ITERATIONS} runs):  {native_total:.1f} ms")
        print(f"  API total ({ITERATIONS} runs):     {api_mem_total:.1f} ms "
              f"({api_mem_se['create_ms']:.1f} ms create + {api_mem_se['total_align_ms']:.1f} ms align)")
        if native_total > 0:
            print(f"  API throughput advantage: {native_total / api_mem_total:.2f}x over {ITERATIONS} runs")
        print()


if __name__ == "__main__":
    main()
