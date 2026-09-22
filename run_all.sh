#!/usr/bin/env bash
# Reproduces every number in README.md / RESULTS.md from a clean checkout.
#
#   ./run_all.sh [--quick] [--rounds N] [--no-gcc] [--no-dotnet]
#
#   --quick      2 rounds, fewer reps, skips the ecosystem harnesses; writes to build/quick/
#                instead of data/ (a smoke test, not a measurement)
#   --rounds N   interleaved rounds of the dependency-free harnesses (default 10)
#   --no-gcc     skip the GCC leg (recorded in the env file, never silent)
#   --no-dotnet  skip the C# legs (recorded in the env file, never silent)
#
# Env: GXX (default g++-16), CXX_CLANG (default clang++).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD="$ROOT/build"
QUICK=0 ROUNDS=10 GCC=1 DOTNET=1
while [[ $# -gt 0 ]]; do
  case "$1" in
    --quick) QUICK=1; ROUNDS=2 ;;
    --rounds) ROUNDS="$2"; shift ;;
    --no-gcc) GCC=0 ;;
    --no-dotnet) DOTNET=0 ;;
    *) echo "unknown option: $1" >&2; exit 64 ;;
  esac
  shift
done
GXX="${GXX:-g++-16}"
CXX_CLANG="${CXX_CLANG:-clang++}"
REPS=200 WARMUP_MS=1000
[[ $QUICK == 1 ]] && REPS=40 WARMUP_MS=300

export CARGO_TARGET_DIR="$BUILD/rust"
export DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1 DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1

step() { printf '\n==> %s\n' "$*"; }
need() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing '$1'. Install: $2" >&2; exit 1; }
}

# ------------------------------------------------------------------ preflight
step "preflight"
need cmake "brew install cmake"
need "$CXX_CLANG" "xcode-select --install"
need cargo "https://rustup.rs"
need python3 "brew install python"
[[ $GCC == 1 ]] && need "$GXX" "brew install gcc   (or pass --no-gcc)"
[[ $DOTNET == 1 ]] && need dotnet "brew install dotnet   (or pass --no-dotnet)"

machine="$( (sysctl -n machdep.cpu.brand_string 2>/dev/null || grep -m1 'model name' /proc/cpuinfo | cut -d: -f2) |
  tr '[:upper:]' '[:lower:]' | sed -E 's/\(r\)|\(tm\)|@.*//g; s/[^a-z0-9]+/-/g; s/^-|-$//g')"
if [[ $QUICK == 1 ]]; then OUT="$BUILD/quick"; else OUT="$ROOT/data"; fi
mkdir -p "$OUT" "$BUILD/dumps"
RAW="$OUT/raw-$machine.csv" ALLOC="$OUT/alloc-$machine.csv"
SUMMARY="$OUT/summary-$machine.md" ENVF="$OUT/env-$machine.txt"
rm -f "$RAW" "$ALLOC"
echo "machine: $machine   output: $OUT   rounds: $ROUNDS   reps: $REPS"

# ---------------------------------------------------------------------- build
step "build C++"
cmake -S "$ROOT/cpp" -B "$BUILD/cpp-clang" -DCMAKE_CXX_COMPILER="$CXX_CLANG" >/dev/null
cmake --build "$BUILD/cpp-clang" -j >/dev/null
if [[ $GCC == 1 ]]; then
  cmake -S "$ROOT/cpp" -B "$BUILD/cpp-gcc" -DCMAKE_CXX_COMPILER="$GXX" >/dev/null
  cmake --build "$BUILD/cpp-gcc" -j >/dev/null
fi

step "build Rust"
(cd "$ROOT/rust" && cargo build --release -q && { [[ $QUICK == 1 ]] || cargo bench --no-run -q; })

AOT_BIN="$BUILD/dotnet/publish/Dis.Bench.Aot/release_osx-arm64/Dis.Bench.Aot"
JIT_DLL="$BUILD/dotnet/bin/Dis.Bench.Aot/release/Dis.Bench.Aot.dll"
if [[ $DOTNET == 1 ]]; then
  step "build C# (CoreCLR + NativeAOT)"
  rid="$(dotnet --info | awk '/RID:/{print $2; exit}')"
  AOT_BIN="$BUILD/dotnet/publish/Dis.Bench.Aot/release_$rid/Dis.Bench.Aot"
  (cd "$ROOT/csharp" &&
    dotnet build Dis.Bench.Aot -c Release -v q -nologo | grep -E ' error ' || true
    dotnet publish Dis.Bench.Aot -c Release -r "$rid" -p:PublishAot=true -v q -nologo | grep -E ' error ' || true
    [[ $QUICK == 1 ]] || dotnet build Dis.Bench -c Release -v q -nologo | grep -E ' error ' || true)
  [[ -x "$AOT_BIN" && -f "$JIT_DLL" ]] || { echo "C# build failed" >&2; exit 1; }
fi

# The dependency-free binaries, in the fixed order each round runs them. Invoked by name so
# paths containing spaces survive (this repo may live under "Mobile Documents").
BINS=(cpp-clang)
[[ $GCC == 1 ]] && BINS+=(cpp-gcc)
BINS+=(rust)
[[ $DOTNET == 1 ]] && BINS+=(csharp-jit csharp-aot)
invoke() {
  local b="$1"; shift
  case "$b" in
    cpp-clang) "$BUILD/cpp-clang/bench" "$@" ;;
    cpp-gcc) "$BUILD/cpp-gcc/bench" "$@" ;;
    rust) "$BUILD/rust/release/bench" "$@" ;;
    csharp-jit) dotnet "$JIT_DLL" "$@" ;;
    csharp-aot) "$AOT_BIN" "$@" ;;
  esac
}

# ---------------------------------------------------------------------- tests
step "unit tests"
"$BUILD/cpp-clang/test_kernels"
[[ $GCC == 1 ]] && "$BUILD/cpp-gcc/test_kernels"
(cd "$ROOT/rust" && cargo test --release -q --lib 2>&1 | grep 'test result')
[[ $DOTNET == 1 ]] && (cd "$ROOT/csharp" && dotnet test --project Dis.Tests -c Release 2>&1 | grep -E 'succeeded:|failed:')
python3 -m unittest discover -s "$ROOT/scripts/tests" 2>&1 | tail -1

step "cross-language equivalence"
dumps=()
for b in "${BINS[@]}"; do
  invoke "$b" dump > "$BUILD/dumps/$b.dump"
  dumps+=("$BUILD/dumps/$b.dump")
done
python3 "$ROOT/scripts/verify.py" "${dumps[@]}"

step "calibrate"
GHZ="$("$BUILD/cpp-clang/bench" calibrate | awk '{print $2}')"
echo "core clock ~ $GHZ GHz"

# ------------------------------------------------ dependency-free, interleaved
step "dependency-free harnesses: $ROUNDS interleaved rounds"
echo "harness,lang,toolchain,kernel,variant,round,rep,ns_per_op" > "$RAW"
for ((r = 1; r <= ROUNDS; r++)); do
  for b in "${BINS[@]}"; do
    invoke "$b" run --round "$r" --reps "$REPS" --warmup-ms "$WARMUP_MS" >> "$RAW" 2>/dev/null
  done
  printf '  round %d/%d done\n' "$r" "$ROUNDS"
done

label() { invoke "$1" run --reps 1 --warmup-ms 0 2>/dev/null | head -1 | cut -d, -f3; }

# ---------------------------------------------------------- ecosystem harnesses
if [[ $QUICK == 0 ]]; then
  for cc in clang gcc; do
    [[ $cc == gcc && $GCC == 0 ]] && continue
    step "Google Benchmark ($cc)"
    "$BUILD/cpp-$cc/bench_gbench" --benchmark_repetitions=20 --benchmark_min_time=0.2s \
      --benchmark_out="$BUILD/gbench-$cc.json" --benchmark_out_format=json >/dev/null
    python3 "$ROOT/scripts/ingest.py" gbench "$BUILD/gbench-$cc.json" --lang cpp \
      --toolchain "$(label "cpp-$cc")" --out "$RAW"
  done

  step "Criterion"
  rm -rf "$CARGO_TARGET_DIR/criterion"
  (cd "$ROOT/rust" && cargo bench -q --bench kernels -- --warm-up-time 2 --measurement-time 5 >/dev/null)
  python3 "$ROOT/scripts/ingest.py" criterion "$CARGO_TARGET_DIR/criterion" --lang rust \
    --toolchain "$(label rust)" --out "$RAW"

  if [[ $DOTNET == 1 ]]; then
    step "BenchmarkDotNet"
    rm -rf "$BUILD/bdn"
    dotnet "$BUILD/dotnet/bin/Dis.Bench/release/Dis.Bench.dll" --filter '*' --exporters fulljson \
      --artifacts "$BUILD/bdn" >/dev/null
    python3 "$ROOT/scripts/ingest.py" bdn "$(ls "$BUILD"/bdn/results/*-report-full.json)" --lang csharp \
      --toolchain "$(label csharp-jit)" --out "$RAW" --alloc "$ALLOC"
  fi
fi

# ----------------------------------------------------- disassembly (mechanism evidence)
if [[ $QUICK == 0 && "$(uname -s)" == Darwin ]]; then
  step "disassembly statistics"
  dis_args=("clang=$BUILD/cpp-clang/bench")
  [[ $GCC == 1 ]] && dis_args+=("gcc=$BUILD/cpp-gcc/bench")
  dis_args+=("rust=$BUILD/rust/release/bench")
  python3 "$ROOT/scripts/disasm.py" "${dis_args[@]}" --out "$OUT/disasm-$machine.md" >/dev/null
fi

# ------------------------------------------------------------ environment record
{
  echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "machine: $machine"
  echo "cpu: $(sysctl -n machdep.cpu.brand_string 2>/dev/null || true)"
  echo "cores: perf=$(sysctl -n hw.perflevel0.physicalcpu 2>/dev/null || echo ?) eff=$(sysctl -n hw.perflevel1.physicalcpu 2>/dev/null || echo ?)"
  echo "os: $(sw_vers -productName 2>/dev/null || uname -s) $(sw_vers -productVersion 2>/dev/null || uname -r)"
  echo "power: $(pmset -g batt 2>/dev/null | head -1 || echo unknown)"
  echo "calibrated_ghz: $GHZ"
  echo "clang: $("$CXX_CLANG" --version | head -1)"
  [[ $GCC == 1 ]] && echo "gcc: $("$GXX" --version | head -1)" || echo "gcc: SKIPPED (--no-gcc)"
  echo "cxx_flags: -std=c++20 -O3 $(grep -o 'DIS_ARCH_FLAGS "[^"]*"' "$ROOT/cpp/CMakeLists.txt" | cut -d'"' -f2) $(grep -o 'DIS_FP_FLAGS "[^"]*"' "$ROOT/cpp/CMakeLists.txt" | cut -d'"' -f2)"
  echo "rustc: $(rustc --version)"
  echo "rust_flags: -C target-cpu=native, release profile lto=fat codegen-units=1"
  if [[ $DOTNET == 1 ]]; then
    echo "dotnet_sdk: $(dotnet --version)"
    echo "dotnet_runtime: $(label csharp-jit)  (TieredPGO on, default)"
  else
    echo "dotnet: SKIPPED (--no-dotnet)"
  fi
  echo "harness: rounds=$ROUNDS reps=$REPS warmup_ms=$WARMUP_MS quick=$QUICK"
} > "$ENVF"

step "summary"
alloc_arg=()
[[ -s "$ALLOC" ]] && alloc_arg=(--alloc "$ALLOC")
python3 "$ROOT/scripts/summarize.py" "$RAW" --ghz "$GHZ" ${alloc_arg[@]+"${alloc_arg[@]}"} --out "$SUMMARY" >/dev/null
echo "raw: $RAW"
echo "summary: $SUMMARY"
echo "env: $ENVF"
