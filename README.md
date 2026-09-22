# dis-realtime-bench

A cross-language benchmark of two real-time simulation kernels, in **C++, Rust and C#**, built to
find out whether "which language is fastest" is even a well-posed question. The kernels come from
IEEE 1278.1 Distributed Interactive Simulation (DIS): decoding an Entity State PDU off the wire,
and body-frame dead reckoning (DRM_RVB).

The value here is the methodology, not a winner. Every number is reproducible with one command,
all implementations are proven to compute bit-identical results before anything is timed, and the
traps that would have produced wrong numbers are written up below, including the ones that did.

## Findings

Apple M3 Pro, macOS 26.6. Median ns per operation, dependency-free harness; full tables with
interquartile ranges, cycles/op and the ecosystem harnesses in [RESULTS.md](RESULTS.md).

| implementation | decode (idiomatic) | decode (shift loop) | dead reckoning |
|---|---:|---:|---:|
| C++, Apple clang 21 | **11.2** | 16.4 | 28.5 |
| C++, GCC 16 | 11.7 | **12.7** | 53.7 |
| Rust 1.95 | 13.2 | 13.3 | **28.0** |
| C# .NET 10, JIT | 21.2 | 29.9 | 49.0 |
| C# .NET 10, NativeAOT | 25.5 | 31.3 | 58.6 |

**1. The compiler can matter more than the language, but not always.** On dead reckoning, the
same C++ source runs 1.88× slower under GCC than under clang, while Rust and clang are within
2% of each other. On idiomatic decode it flips: GCC and clang tie, and the gap to Rust (1.19×)
is the larger one. "Language X is N× faster" from one toolchain and one kernel reports the
toolchain and the kernel.

**2. The mechanisms are mundane, and checkable.** GCC's dead-reckoning deficit is not "worse
codegen." It lowers each `sin`/`cos` pair to a `cexp` call, because it does not recognise Darwin's
`__sincos_stret`, and `cexp` costs 3 ns more per call. That accounts for about half the gap. The
other half is not explained, and this README does not guess. See
[RESULTS.md § Mechanisms](RESULTS.md#mechanisms-verified-in-disassembly-or-by-microbenchmark).

**3. Idiomatic Rust cannot accidentally write the slow version.** Rust's hand-rolled shift loop and
`u32::from_be_bytes` compile to *identical* machine code. In C++ the same choice is worth 1.47×
under clang, because clang does not fuse the 8-byte shift loop, and 1.08× under GCC. That is a
real, defensible ergonomic difference. "Rust is faster than C++" is not supported: C++ decodes
faster here.

**4. C# lands where modern .NET should,** 1.7–1.9× native with `Span<T>`, `BinaryPrimitives`,
struct PDUs, and zero allocation verified by BenchmarkDotNet. NativeAOT is *slower* than the JIT
on this machine. Dynamic PGO explains most of that on decode, but not on dead reckoning.

**5. Two harnesses per language agree within 0.96–1.10×** (Google Benchmark, Criterion,
BenchmarkDotNet versus a dependency-free harness). They disagreed by 2× until trap 4 below was
found.

## The traps

An unfair benchmark rarely looks unfair. It looks like a result. Each of these produced a
plausible-looking number that was wrong.

**1. Dead code elimination: the 1.8 ns decode.** The first Rust measurement in the preliminary run
decoded a PDU in 1.8 ns. That is ~4 cycles to bounds-check and byte-swap ~40 fields, which is
physically impossible and is the only reason it got caught. The benchmark read two fields of the
result, and `black_box` was on the *input*, so LLVM deleted the work producing the other ~20.
The fix is to barrier the *whole* decoded struct (`black_box(es)`, `asm volatile` in C++, an
escaping store in C#). The sanity check that caught it was arithmetic: how many cycles is that?
`bench calibrate` now measures the clock, every table reports cycles/op, and the summary flags
anything under 2 cycles.

**2. Unequal barriers: bounds checks folded away.** The C++ seed barriered the input *pointer* but
passed the length as the constant 144, so clang proved every bounds check true and deleted them.
Rust's `black_box(slice)` hides pointer and length. Laundering the C++ length through `asm`
moved clang from 5.7 to 11.1 ns. The C++ number had been measuring *unchecked* decode against
checked Rust, the exact comparison the methodology forbids.

**3. Different answers: FMA contraction.** Clang and GCC fuse `a*b+c` into one FMA instruction
by default; rustc and RyuJIT never do. The equivalence check passed anyway, because dead-reckoning
outputs are ~10⁶ m and a last-bit difference in a ~0.5 m displacement is rounded away. Dumping
displacement alone exposed 881 of 1,024 results differing by one ULP. C++ now builds with
`-ffp-contract=off`, which costs clang about 12% on dead reckoning (25.6 to 28.7 ns). The timed binary must be the
verified binary.

**4. The inliner's opinion: a 2× swing between harnesses.** Whether the decoder is inlined into
the timed loop depended on the *harness*. Clang never inlined it, GCC always did, and Rust did
under Criterion but not in the plain harness. Identical kernels timed 2× apart. The fix is an
explicit, identical boundary in every language (`[[gnu::noinline]]`, `#[inline(never)]`,
`MethodImplOptions.NoInlining`). This measures one call, as a real receive path would make.
Inlined-loop performance is a different experiment, not measured here.

**5. JIT warmup: tier-0 is 19× slower.** With 200 ms of warmup the C# JIT harness measured
440 ns per PDU, because tiered compilation had not yet promoted the loop. From ~300 ms on it
measured 22 ns. Every harness now warms up for at least 1 s. BenchmarkDotNet handles this for you;
a hand-rolled `Stopwatch` loop does not.

**6. A build flag that changes the runtime.** Setting `PublishAot` in the `.csproj` also writes
AOT feature switches (`IsDynamicCodeSupported=false`, size-optimised LINQ) into the *JIT* build's
runtime config. The "JIT" measurement was no longer a default CoreCLR app. `PublishAot` is now
passed only at publish time.

**7. Equivalent safety.** All three languages bounds-check every read. C++ `std::span` would not
by default, so the C++ reader checks explicitly. Checked Rust against unchecked C++ is not a
comparison.

**8. Measurement hygiene on macOS.** There is no `taskset`. macOS will not pin a thread to a core,
and the Apple Silicon clock ticks at 24 MHz (41.7 ns), so only whole 1,024-operation batches are
timed. Binaries run interleaved across 10 rounds so drift hits all of them equally. Medians and
IQRs, never means. During development one benchmark showed two stable timing modes with identical
machine code, and the cause was not found ([RESULTS.md](RESULTS.md#observed-but-unexplained-bimodal-timings)).

## Limitations

- **Two kernels are not a language.** Both are small, scalar, and allocation-free.
- **One machine is not a population.** Apple M3 Pro only. The preliminary numbers that motivated
  this repo were from an x86 cloud VM, where clang beat GCC 4.7× on decode. That did not reproduce
  on arm64. The seed harness that produced it had traps 2 and 4 unaddressed, so those numbers
  cannot be taken at face value.
- **Imperfect run conditions.** The committed run was on battery with normal desktop load.
  Differences under ~10% are noise.
- **A cache-resident corpus is not production.** 147 KB fits in cache by design, to measure the
  codec rather than memory bandwidth.
- **Unmeasured:** multi-threaded scaling, allocation-heavy code, startup time, compile time,
  binary size, and inlined-loop performance.
- NativeAOT is measured by one harness only. BenchmarkDotNet covers the JIT.

## Layout

```
cpp/        kernels.hpp (shared kernels), bench.cpp (dependency-free), bench_gbench.cpp, tests
rust/       src/lib.rs (kernels + tests), src/main.rs (dependency-free), benches/kernels.rs (Criterion)
csharp/     Dis.Kernels, Dis.Tests (xUnit), Dis.Bench.Aot (dependency-free; JIT and NativeAOT),
            Dis.Bench (BenchmarkDotNet)
scripts/    verify.py (equivalence gate), ingest.py, summarize.py, disasm.py, tests/
data/       raw samples, summary tables, disassembly statistics, environment record
run_all.sh  one command: build, test, verify, calibrate, measure, summarize
```

The three dependency-free harnesses are deliberately parallel, so the diff between them is the
language and nothing else.

## Reproducing

Requirements: CMake ≥ 3.24, Apple clang or clang, GCC (the `--no-gcc` flag skips it), Rust 1.95+,
.NET 10 SDK (the `--no-dotnet` flag skips it), Python 3. On macOS with Homebrew:

```sh
brew install cmake gcc dotnet
./run_all.sh            # full run, about 5 minutes; writes data/
./run_all.sh --quick    # smoke test, about 20 seconds; writes build/quick/
```

`run_all.sh` fails if any unit test fails, if any implementation's output differs from the others
by a single bit, or if a C# benchmark allocates. Third-party dependencies are pinned: Google
Benchmark by tarball SHA-256, Criterion in `Cargo.lock`, and NuGet packages in `packages.lock.json`.

Homebrew's .NET links NativeAOT binaries against Homebrew OpenSSL and Brotli without passing their
library paths. `Dis.Bench.Aot.csproj` adds them when Homebrew is present.

## License

MIT
