// Google Benchmark harness, C++ side. Same kernels and batch shape as bench.cpp: one iteration
// = one 1024-PDU batch, so ns/op = iteration time / 1024 (scripts/ingest.py does the division).
#include <benchmark/benchmark.h>

#include "kernels.hpp"

namespace {

const std::vector<std::uint8_t>& corpus() {
  static const auto c = dis::build_corpus();
  return c;
}
const dis::DrInputs& dr_inputs() {
  static const auto d = dis::build_dr_inputs();
  return d;
}

template <class L>
void BM_Decode(benchmark::State& state) {
  const auto& c = corpus();
  dis::EntityState es{};
  for (auto _ : state) {
    std::size_t decoded = 0;
    for (std::size_t i = 0; i < dis::kCount; ++i) {
      const std::uint8_t* p = c.data() + i * dis::kEspduSize;
      std::size_t len = dis::kEspduSize;
      // Non-const DoNotOptimize is "+r,m": the optimizer must treat p and len as unknown,
      // so the bounds checks stay (same as launder() in bench.cpp / black_box(slice) in Rust).
      benchmark::DoNotOptimize(p);
      benchmark::DoNotOptimize(len);
      if (dis::decode_espdu<L>(p, len, es)) {
        benchmark::DoNotOptimize(es);  // the whole struct, not just the fields we read
        ++decoded;
      }
    }
    if (decoded != dis::kCount) state.SkipWithError("self-check failed: not every PDU decoded");
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * dis::kCount));
}

void BM_Dr(benchmark::State& state) {
  const auto& in = dr_inputs();
  for (auto _ : state) {
    benchmark::ClobberMemory();
    double local = 0.0;
    double out[3];
    for (std::size_t i = 0; i < dis::kCount; ++i) {
      dis::drm_rvb(&in.p0[i * 3], &in.v0[i * 3], &in.a0[i * 3], &in.w[i * 3], in.eul[i * 3],
                   in.eul[i * 3 + 1], in.eul[i * 3 + 2], dis::kDrDt, out);
      local += out[0] + out[1] + out[2];
    }
    benchmark::DoNotOptimize(local);
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * dis::kCount));
}

}  // namespace

BENCHMARK(BM_Decode<dis::LoadBswap>)->Name("decode/idiomatic");
BENCHMARK(BM_Decode<dis::LoadShift>)->Name("decode/shift");
BENCHMARK(BM_Dr)->Name("dr/default");

BENCHMARK_MAIN();
