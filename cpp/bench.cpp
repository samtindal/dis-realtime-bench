// Dependency-free harness, C++ side. Parallel in structure to rust/src/main.rs and
// csharp/Dis.Bench.Aot/Program.cs, so the diff between them is the language and nothing else.
//
//   bench run [--toolchain LABEL] [--round N] [--reps 25] [--warmup-ms 500]
//   bench dump
//   bench calibrate
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "kernels.hpp"

namespace {

// Equivalent of Rust's std::hint::black_box / benchmark::DoNotOptimize. There is no
// standard barrier in C++20. Applied to the WHOLE decoded struct: barriering only the
// fields you read lets the optimizer delete the work that produced the others.
template <typename T>
inline void do_not_optimize(T const& value) {
  __asm__ __volatile__("" : : "r,m"(value) : "memory");
}
inline void clobber_memory() { __asm__ __volatile__("" : : : "memory"); }

// Makes a value opaque: the optimizer must assume the asm may have changed it. Used on the
// decode input's pointer AND length, the C++ equivalent of Rust's black_box(slice). Without
// it the length is the constant 144 and every bounds check folds away at compile time.
template <typename T>
inline void launder(T& value) {
  __asm__ __volatile__("" : "+r"(value));
}

std::string default_toolchain() {
#if defined(__apple_build_version__)
  return "apple-clang-" + std::to_string(__clang_major__) + "." +
         std::to_string(__clang_minor__) + "." + std::to_string(__clang_patchlevel__);
#elif defined(__clang__)
  return "clang-" + std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__) +
         "." + std::to_string(__clang_patchlevel__);
#elif defined(__GNUC__)
  return "gcc-" + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__) + "." +
         std::to_string(__GNUC_PATCHLEVEL__);
#else
  return "unknown";
#endif
}

using Clock = std::chrono::steady_clock;

double elapsed_ns(Clock::time_point a, Clock::time_point b) {
  return std::chrono::duration<double, std::nano>(b - a).count();
}

// One batch = the whole 1024-PDU corpus. Returns how many PDUs decoded, for the self-check.
template <class L>
std::size_t decode_batch(const std::vector<std::uint8_t>& corpus, double& acc) {
  clobber_memory();
  dis::EntityState es{};
  std::size_t decoded = 0;
  double local = 0.0;
  for (std::size_t i = 0; i < dis::kCount; ++i) {
    const std::uint8_t* p = corpus.data() + i * dis::kEspduSize;
    std::size_t len = dis::kEspduSize;
    launder(p);
    launder(len);
    if (dis::decode_espdu<L>(p, len, es)) {
      do_not_optimize(es);
      local += es.location[0] + static_cast<double>(es.entity);
      ++decoded;
    }
  }
  do_not_optimize(local);
  acc += local;
  return decoded;
}

std::size_t dr_batch(const dis::DrInputs& in, double& acc) {
  clobber_memory();
  double local = 0.0;
  double out[3];
  for (std::size_t i = 0; i < dis::kCount; ++i) {
    dis::drm_rvb(&in.p0[i * 3], &in.v0[i * 3], &in.a0[i * 3], &in.w[i * 3], in.eul[i * 3],
                 in.eul[i * 3 + 1], in.eul[i * 3 + 2], dis::kDrDt, out);
    local += out[0] + out[1] + out[2];
  }
  do_not_optimize(local);
  acc += local;
  return dis::kCount;
}

struct Options {
  std::string toolchain = default_toolchain();
  int round = 0;
  int reps = 25;
  int warmup_ms = 500;
};

template <class Batch>
void measure(const Options& o, const char* kernel, const char* variant, Batch&& batch) {
  // Warmup: at least 5 batches AND at least warmup_ms, identical rule in every language.
  const auto w0 = Clock::now();
  for (int n = 0; n < 5 || elapsed_ns(w0, Clock::now()) < o.warmup_ms * 1e6; ++n) batch();
  for (int rep = 0; rep < o.reps; ++rep) {
    const auto t0 = Clock::now();
    const std::size_t done = batch();
    const auto t1 = Clock::now();
    if (done != dis::kCount) {
      std::fprintf(stderr, "self-check failed: %s/%s processed %zu of %zu\n", kernel, variant,
                   done, dis::kCount);
      std::exit(2);
    }
    std::printf("simple,cpp,%s,%s,%s,%d,%d,%.4f\n", o.toolchain.c_str(), kernel, variant,
                o.round, rep, elapsed_ns(t0, t1) / static_cast<double>(dis::kCount));
  }
}

int cmd_run(const Options& o) {
  const auto corpus = dis::build_corpus();
  const auto dr_in = dis::build_dr_inputs();
  double acc = 0.0;
  measure(o, "decode", dis::LoadBswap::name,
          [&] { return decode_batch<dis::LoadBswap>(corpus, acc); });
  measure(o, "decode", dis::LoadShift::name,
          [&] { return decode_batch<dis::LoadShift>(corpus, acc); });
  measure(o, "dr", "default", [&] { return dr_batch(dr_in, acc); });
  std::fprintf(stderr, "checksum %.6e\n", acc);
  return 0;
}

// Estimates the core clock from a chain of dependent adds (1 cycle latency each on every
// modern core), so ns/op can be converted to cycles/op. The sanity check that caught the
// DCE bug was exactly this arithmetic: "how many cycles is that, and is it possible?"
int cmd_calibrate() {
  constexpr std::uint64_t kIters = 25'000'000;  // x16 adds per iteration
  double best = 0.0;
  for (int trial = 0; trial < 7; ++trial) {
    std::uint64_t x = 0, n = kIters;
    const auto t0 = Clock::now();
#if defined(__aarch64__)
    __asm__ __volatile__(
        "1:\n"
        ".rept 16\n add %0, %0, #1\n .endr\n"
        "subs %1, %1, #1\n"
        "b.ne 1b\n"
        : "+r"(x), "+r"(n));
#elif defined(__x86_64__)
    __asm__ __volatile__(
        "1:\n"
        ".rept 16\n add $1, %0\n .endr\n"
        "dec %1\n"
        "jnz 1b\n"
        : "+r"(x), "+r"(n));
#else
#error "calibrate: unsupported architecture"
#endif
    const auto t1 = Clock::now();
    do_not_optimize(x);
    const double ghz = static_cast<double>(kIters * 16) / elapsed_ns(t0, t1);
    best = std::max(best, ghz);
  }
  std::printf("ghz %.3f\n", best);
  return 0;
}

int usage() {
  std::fprintf(stderr,
               "usage: bench run [--toolchain L] [--round N] [--reps N] [--warmup-ms N]\n"
               "       bench dump\n"
               "       bench calibrate\n");
  return 64;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return usage();
  const std::string cmd = argv[1];
  if (cmd == "dump") {
    dis::write_dump(stdout);
    return 0;
  }
  if (cmd == "calibrate") return cmd_calibrate();
  if (cmd != "run") return usage();
  Options o;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (i + 1 >= argc) return usage();
    const char* v = argv[++i];
    if (a == "--toolchain") o.toolchain = v;
    else if (a == "--round") o.round = std::atoi(v);
    else if (a == "--reps") o.reps = std::atoi(v);
    else if (a == "--warmup-ms") o.warmup_ms = std::atoi(v);
    else return usage();
  }
  return cmd_run(o);
}
