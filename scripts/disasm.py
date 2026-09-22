#!/usr/bin/env python3
"""Per-function instruction statistics for the benchmarked kernels (macOS/arm64, otool).

The kernels are never inlined (see cpp/include/kernels.hpp), so the functions disassembled here
are exactly the ones the timed loops call.

usage: disasm.py LABEL=BINARY [LABEL=BINARY ...] [--out FILE.md]
"""
import re
import subprocess
import sys
from collections import Counter

FUNCS = {"decode_espdu": re.compile(r"decode_espdu"), "drm_rvb": re.compile(r"drm_rvb")}
COND_BRANCH = re.compile(r"^(b\.[a-z]+|cbz|cbnz|tbz|tbnz)$")


def demangle(names):
    out = subprocess.run(["c++filt"], input="\n".join(names), capture_output=True, text=True).stdout
    return out.splitlines()


def functions(binary):
    text = subprocess.run(["otool", "-tvV", binary], capture_output=True, text=True, check=True).stdout
    funcs, name = {}, None
    for line in text.splitlines():
        if line and not line[0].isspace() and line.endswith(":") and not line.startswith(binary):
            name = line[:-1]
            funcs[name] = []
        elif name and "\t" in line:
            parts = line.split("\t")
            if len(parts) >= 2:
                funcs[name].append((parts[1].strip(), "\t".join(parts[2:]).strip()))
    mangled = list(funcs)
    return dict(zip(demangle([m[1:] if m.startswith("__Z") else m for m in mangled]), funcs.values()))


def stats(insns):
    ops = Counter(op for op, _ in insns)
    calls = Counter()
    for op, args in insns:
        if op == "bl":
            m = re.search(r"symbol stub for: (\S+)", args)
            calls[(m.group(1) if m else args.split()[-1]).lstrip("_")] += 1
    return {
        "insns": len(insns),
        "cond_branches": sum(n for op, n in ops.items() if COND_BRANCH.match(op)),
        "rev": sum(n for op, n in ops.items() if op.startswith("rev")),
        "loads": sum(n for op, n in ops.items() if op.startswith("ld")),
        "stores": sum(n for op, n in ops.items() if op.startswith("st")),
        "fdiv": ops.get("fdiv", 0),
        "fmadd": sum(n for op, n in ops.items() if op in ("fmadd", "fmsub", "fnmadd", "fnmsub")),
        "calls": ", ".join(f"{k}×{v}" for k, v in sorted(calls.items())) or "—",
    }


def main(argv):
    out_path = None
    if "--out" in argv:
        i = argv.index("--out")
        out_path = argv[i + 1]
        argv = argv[:i] + argv[i + 2:]
    rows = ["| binary | function | insns | cond branches | rev | loads | stores | fdiv | fmadd | calls |",
            "|---|---|---:|---:|---:|---:|---:|---:|---:|---|"]
    for spec in argv:
        label, _, binary = spec.partition("=")
        for name, insns in sorted(functions(binary).items()):
            if not any(p.search(name) for p in FUNCS.values()) or "dump" in name:
                continue
            short = re.sub(r"\(.*\)", "", name.split("::")[-1] if "::" in name else name)
            short = short.replace("unsigned char const*, unsigned long, dis::EntityState&", "")
            s = stats(insns)
            rows.append(f"| {label} | `{short}` | {s['insns']} | {s['cond_branches']} | {s['rev']} | "
                        f"{s['loads']} | {s['stores']} | {s['fdiv']} | {s['fmadd']} | {s['calls']} |")
    md = "\n".join(rows) + "\n"
    if out_path:
        open(out_path, "w").write(md)
    print(md)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
