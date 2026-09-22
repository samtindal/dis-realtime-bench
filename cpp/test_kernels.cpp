// Edge-case tests for the C++ kernels. No framework: CHECK records failures, exit code reports.
#include <cstdio>
#include <cstring>

#include "kernels.hpp"

static int g_failures = 0;
#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                          \
    }                                                                        \
  } while (0)

template <class L>
static void test_decodes_known_fields() {
  const auto c = dis::build_corpus();
  dis::EntityState es{};
  CHECK(dis::decode_espdu<L>(c.data(), dis::kEspduSize, es));
  CHECK(es.site == 1 && es.application == 2 && es.entity == 0);
  CHECK(es.force_id == 1 && es.kind == 1 && es.domain == 1 && es.country == 225);
  CHECK(es.velocity[0] == 10.0f && es.velocity[2] == 12.0f);
  CHECK(es.location[0] == 1.0e6 && es.location[2] == 1.0e6 + 2.0);
  CHECK(es.orientation[1] == 0.2f);
  CHECK(es.dr_algorithm == 8);
  CHECK(es.lin_accel[1] == 0.5f && es.ang_vel[2] == 0.01f);
  CHECK(std::memcmp(es.marking, "ABCDEFGHIJK", 11) == 0);
}

template <class L>
static void test_every_pdu_decodes_with_its_index() {
  const auto c = dis::build_corpus();
  dis::EntityState es{};
  for (std::size_t i = 0; i < dis::kCount; ++i) {
    CHECK(dis::decode_espdu<L>(c.data() + i * dis::kEspduSize, dis::kEspduSize, es));
    CHECK(es.entity == static_cast<std::uint16_t>(i));
  }
}

template <class L>
static void test_rejects_malformed() {
  const auto c = dis::build_corpus();
  dis::EntityState es{};
  for (std::size_t n = 0; n < dis::kEspduSize; ++n) CHECK(!dis::decode_espdu<L>(c.data(), n, es));
  auto bad_type = c;
  bad_type[2] = 2;
  CHECK(!dis::decode_espdu<L>(bad_type.data(), dis::kEspduSize, es));
  auto short_len = c;
  short_len[9] = 143;
  CHECK(!dis::decode_espdu<L>(short_len.data(), dis::kEspduSize, es));
}

static void test_idioms_agree() {
  const auto c = dis::build_corpus();
  for (std::size_t i = 0; i < dis::kCount; ++i) {
    dis::EntityState a{}, b{};
    const auto* p = c.data() + i * dis::kEspduSize;
    CHECK(dis::decode_espdu<dis::LoadBswap>(p, dis::kEspduSize, a));
    CHECK(dis::decode_espdu<dis::LoadShift>(p, dis::kEspduSize, b));
    CHECK(std::memcmp(a.location, b.location, sizeof a.location) == 0);
    CHECK(a.appearance == b.appearance && a.capabilities == b.capabilities);
  }
}

static void test_dr_zero_omega_is_kinematic() {
  const double p0[3] = {1.0e6, 2.0e6, 3.0e6}, v0[3] = {1.0, 0.0, 0.0}, a0[3] = {0.0, 0.0, 0.0};
  const double w[3] = {0.0, 0.0, 0.0};
  double out[3];
  dis::drm_rvb(p0, v0, a0, w, 0.0, 0.0, 0.0, 2.0, out);
  CHECK(out[0] == 1.0e6 + 2.0 && out[1] == 2.0e6 && out[2] == 3.0e6);
}

static void test_corpus_hash_is_stable() {
  const auto c = dis::build_corpus();
  CHECK(c.size() == dis::kCount * dis::kEspduSize);
  CHECK(dis::fnv1a64(reinterpret_cast<const std::uint8_t*>("a"), 1) == 0xaf63dc4c8601ec8cULL);
}

int main() {
  test_decodes_known_fields<dis::LoadBswap>();
  test_decodes_known_fields<dis::LoadShift>();
  test_every_pdu_decodes_with_its_index<dis::LoadBswap>();
  test_every_pdu_decodes_with_its_index<dis::LoadShift>();
  test_rejects_malformed<dis::LoadBswap>();
  test_rejects_malformed<dis::LoadShift>();
  test_idioms_agree();
  test_dr_zero_omega_is_kinematic();
  test_corpus_hash_is_stable();
  if (g_failures) {
    std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
  }
  std::puts("test_kernels: all checks passed");
  return 0;
}
