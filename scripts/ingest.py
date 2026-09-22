#!/usr/bin/env python3
"""Normalise ecosystem-harness output into the shared raw CSV.

Every harness times one batch of 1024 operations per iteration; rows are ns per operation.

usage:
  ingest.py gbench    FILE --lang L --toolchain T --out RAW.csv
  ingest.py criterion DIR  --lang L --toolchain T --out RAW.csv      (DIR = .../criterion)
  ingest.py bdn       FILE --lang L --toolchain T --out RAW.csv --alloc ALLOC.csv
"""
import argparse
import csv
import json
from pathlib import Path

BATCH = 1024
RAW_FIELDS = ["harness", "lang", "toolchain", "kernel", "variant", "round", "rep", "ns_per_op"]
ALLOC_FIELDS = ["lang", "toolchain", "kernel", "variant", "bytes_per_op", "gen0"]
KNOWN = {("decode", "idiomatic"), ("decode", "shift"), ("dr", "default")}
BDN_METHODS = {"DecodeIdiomatic": ("decode", "idiomatic"), "DecodeShift": ("decode", "shift"),
               "Dr": ("dr", "default")}
UNIT_NS = {"ns": 1.0, "us": 1e3, "ms": 1e6, "s": 1e9}


def split_name(name):
    kernel, _, variant = name.partition("/")
    if (kernel, variant) not in KNOWN:
        raise ValueError(f"unknown benchmark name {name!r}")
    return kernel, variant


def append(path, fields, rows):
    path = Path(path)
    new = not path.exists() or path.stat().st_size == 0
    with open(path, "a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        if new:
            w.writeheader()
        w.writerows(rows)


def row(harness, lang, toolchain, kernel, variant, rep, ns):
    return {"harness": harness, "lang": lang, "toolchain": toolchain, "kernel": kernel,
            "variant": variant, "round": 0, "rep": rep, "ns_per_op": f"{ns:.4f}"}


def ingest_gbench(src, lang, toolchain, out):
    data = json.loads(Path(src).read_text())
    rows = []
    for b in data["benchmarks"]:
        if b.get("run_type") != "iteration":
            continue  # aggregates (mean/median/stddev) are recomputed from raw rows
        kernel, variant = split_name(b["name"])
        ns = b["real_time"] * UNIT_NS[b["time_unit"]] / BATCH
        rows.append(row("gbench", lang, toolchain, kernel, variant, b.get("repetition_index", 0), ns))
    append(out, RAW_FIELDS, rows)
    return len(rows)


def ingest_criterion(root, lang, toolchain, out):
    rows = []
    for sample in sorted(Path(root).glob("*/*/new/sample.json")):
        meta = json.loads((sample.parent / "benchmark.json").read_text())
        kernel, variant = split_name(f"{meta['group_id']}/{meta['function_id']}")
        s = json.loads(sample.read_text())
        for rep, (iters, total_ns) in enumerate(zip(s["iters"], s["times"])):
            rows.append(row("criterion", lang, toolchain, kernel, variant, rep, total_ns / iters / BATCH))
    append(out, RAW_FIELDS, rows)
    return len(rows)


def ingest_bdn(src, lang, toolchain, out, alloc_out):
    data = json.loads(Path(src).read_text())
    rows, alloc = [], []
    for b in data["Benchmarks"]:
        if b["Method"] not in BDN_METHODS:
            raise ValueError(f"unknown BDN method {b['Method']!r}")
        kernel, variant = BDN_METHODS[b["Method"]]
        # OriginalValues are already per operation (BDN divides by OperationsPerInvoke).
        for rep, ns in enumerate(b["Statistics"]["OriginalValues"]):
            rows.append(row("bdn", lang, toolchain, kernel, variant, rep, ns))
        mem = b.get("Memory") or {}
        alloc.append({"lang": lang, "toolchain": toolchain, "kernel": kernel, "variant": variant,
                      "bytes_per_op": mem.get("BytesAllocatedPerOperation"),
                      "gen0": mem.get("Gen0Collections")})
    append(out, RAW_FIELDS, rows)
    append(alloc_out, ALLOC_FIELDS, alloc)
    return len(rows)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("kind", choices=["gbench", "criterion", "bdn"])
    p.add_argument("src")
    p.add_argument("--lang", required=True)
    p.add_argument("--toolchain", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--alloc")
    a = p.parse_args()
    if a.kind == "gbench":
        n = ingest_gbench(a.src, a.lang, a.toolchain, a.out)
    elif a.kind == "criterion":
        n = ingest_criterion(a.src, a.lang, a.toolchain, a.out)
    else:
        if not a.alloc:
            p.error("bdn requires --alloc")
        n = ingest_bdn(a.src, a.lang, a.toolchain, a.out, a.alloc)
    if n == 0:
        raise SystemExit(f"ingest {a.kind}: no rows found in {a.src}")
    print(f"ingest {a.kind}: {n} rows ({a.toolchain})")


if __name__ == "__main__":
    main()
