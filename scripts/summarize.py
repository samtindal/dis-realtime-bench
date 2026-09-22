#!/usr/bin/env python3
"""Raw samples -> markdown summary tables.

usage: summarize.py RAW.csv --ghz GHZ [--alloc ALLOC.csv] [--out SUMMARY.md]

Reports median and interquartile range (never the mean: one preemption ruins a mean), the
minimum, and cycles/op at the calibrated clock. Exits 1 if any BenchmarkDotNet row allocated:
a C# number with allocations is an allocator benchmark, not a codec benchmark.
"""
import argparse
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path

DCE_FLOOR_CYCLES = 2.0  # nothing in these kernels is plausible below this


def quartiles(v):
    if len(v) == 1:
        return v[0], v[0]
    q = statistics.quantiles(v, n=4, method="inclusive")
    return q[0], q[2]


def summarize(raw_path, ghz, alloc_path=None):
    groups = defaultdict(list)
    with open(raw_path, newline="") as f:
        for r in csv.DictReader(f):
            key = (r["kernel"], r["variant"], r["harness"], r["lang"], r["toolchain"])
            groups[key].append(float(r["ns_per_op"]))

    stats = {}
    for key, v in groups.items():
        v.sort()
        p25, p75 = quartiles(v)
        stats[key] = {"median": statistics.median(v), "p25": p25, "p75": p75, "min": v[0], "n": len(v)}

    out = [f"Clock for cycles/op: {ghz:.3f} GHz (scripts: bench calibrate).", ""]
    for kv in sorted({(k[0], k[1]) for k in stats}):
        out += [f"### {kv[0]} / {kv[1]} (ns per op)", "",
                "| harness | lang | toolchain | median | p25–p75 | min | cycles/op | n |",
                "|---|---|---|---:|---:|---:|---:|---:|"]
        rows = sorted((k for k in stats if (k[0], k[1]) == kv), key=lambda k: stats[k]["median"])
        for k in rows:
            s = stats[k]
            cyc = s["median"] * ghz
            flag = " ⚠ DCE?" if cyc < DCE_FLOOR_CYCLES else ""
            out.append(f"| {k[2]} | {k[3]} | {k[4]} | {s['median']:.2f} | {s['p25']:.2f}–{s['p75']:.2f} "
                       f"| {s['min']:.2f} | {cyc:.1f}{flag} | {s['n']} |")
        out.append("")

    # Harness agreement: each ecosystem harness vs the dependency-free one, same implementation.
    agree = []
    for k, s in sorted(stats.items()):
        if k[2] == "simple":
            continue
        base = stats.get((k[0], k[1], "simple", k[3], k[4]))
        if base:
            agree.append(f"| {k[3]} | {k[4]} | {k[0]}/{k[1]} | {k[2]} | {base['median']:.2f} "
                         f"| {s['median']:.2f} | {s['median'] / base['median']:.2f} |")
    if agree:
        out += ["### Harness agreement (ecosystem median / dependency-free median)", "",
                "| lang | toolchain | kernel | harness | simple | ecosystem | ratio |",
                "|---|---|---|---|---:|---:|---:|", *agree, ""]

    ok = True
    if alloc_path:
        out += ["### Allocation (BenchmarkDotNet MemoryDiagnoser)", "",
                "| lang | toolchain | kernel | variant | bytes/op | Gen0 |", "|---|---|---|---|---:|---:|"]
        with open(alloc_path, newline="") as f:
            for r in csv.DictReader(f):
                b = float(r["bytes_per_op"] or 0)
                if b > 0:
                    ok = False
                out.append(f"| {r['lang']} | {r['toolchain']} | {r['kernel']} | {r['variant']} "
                           f"| {r['bytes_per_op']} | {r['gen0']} |")
        out.append("")
    return "\n".join(out), ok


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("raw")
    p.add_argument("--ghz", type=float, required=True)
    p.add_argument("--alloc")
    p.add_argument("--out")
    a = p.parse_args()
    md, ok = summarize(a.raw, a.ghz, a.alloc)
    if a.out:
        Path(a.out).write_text(md)
    print(md)
    if not ok:
        print("FAIL: a C# benchmark allocated; its timings are not codec measurements", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
