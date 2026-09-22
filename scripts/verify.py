#!/usr/bin/env python3
"""Cross-language equivalence check.

Every dependency-free harness binary prints a canonical dump (`<bin> dump`): corpus hash,
every decoded ESPDU field (floats as IEEE-754 bit patterns), malformed-input outcomes, and
every dead-reckoning output. All dumps must be byte-identical, so "equal" means
bit-identical, not "close". Timing numbers mean nothing until this passes.

usage: verify.py DUMP [DUMP ...]      exit 0 if all agree and edge cases are correct
"""
import sys
from pathlib import Path

EXPECTED_EDGES = {
    "truncated_accepts": "0",
    "full_accepts": "1",
    "bad_type_accepts": "0",
    "short_length_accepts": "0",
}


def check_content(path, lines):
    errors = []
    if not lines:
        return [f"{path}: empty dump"]
    for n, line in enumerate(lines, 1):
        parts = line.split()
        if "DECODE_FAILED" in parts:
            errors.append(f"{path} line {n}: {line}")
        if parts and parts[0] == "edge" and len(parts) == 4:
            want = EXPECTED_EDGES.get(parts[2])
            if want is not None and parts[3] != want:
                errors.append(f"{path} line {n}: {parts[2]} = {parts[3]}, expected {want}")
    return errors


def verify(paths):
    paths = [Path(p) for p in paths]
    if len(paths) < 2:
        return ["need at least two dumps to compare"]
    contents = {p: p.read_text().splitlines() for p in paths}
    errors = []
    for p, lines in contents.items():
        errors += check_content(p, lines)

    ref = paths[0]
    ref_lines = contents[ref]
    for p in paths[1:]:
        lines = contents[p]
        for n, (a, b) in enumerate(zip(ref_lines, lines), 1):
            if a != b:
                errors.append(f"{p} differs from {ref} at line {n}:\n  {ref.name}: {a}\n  {p.name}: {b}")
                break
        else:
            if len(lines) != len(ref_lines):
                errors.append(f"{p} has {len(lines)} lines, {ref} has {len(ref_lines)}")
    return errors


def main(argv):
    errors = verify(argv[1:])
    for e in errors:
        print(f"FAIL {e}", file=sys.stderr)
    if errors:
        return 1
    n = len(Path(argv[1]).read_text().splitlines())
    print(f"verify: {len(argv) - 1} dumps bit-identical ({n} lines each), edge cases correct")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
