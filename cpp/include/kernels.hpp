// DIS benchmark kernels, C++ side.
//   (1) Entity State PDU decode, with two byte-read idioms selectable by policy.
//   (2) Body-frame dead reckoning, DRM_RVB (IEEE 1278.1 Annex E).
// Shared by the dependency-free harness (bench.cpp), the Google Benchmark harness
// (bench_gbench.cpp) and the tests, so every harness measures the same code.
#pragma once

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace dis {

static_assert(std::endian::native == std::endian::little,
              "LoadBswap assumes a little-endian host");

// ------------------------------------------------------------ byte-read idioms
// "shift": the hand-rolled pattern. Whether it becomes a single load + byte swap
// depends entirely on the compiler recognising the idiom.
struct LoadShift {
  static constexpr const char* name = "shift";
  static std::uint16_t u16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) |
                                      static_cast<std::uint16_t>(p[1]));
  }
  static std::uint32_t u32(const std::uint8_t* p) noexcept {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
  }
  static std::uint64_t u64(const std::uint8_t* p) noexcept {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | static_cast<std::uint64_t>(p[i]);
    return v;
  }
};

// "idiomatic": one unaligned primitive load (memcpy of a single scalar, not of a wire
// struct) followed by an explicit byte swap. C++20 has no std::byteswap, hence the builtin.
struct LoadBswap {
  static constexpr const char* name = "idiomatic";
  static std::uint16_t u16(const std::uint8_t* p) noexcept {
    std::uint16_t v;
    std::memcpy(&v, p, sizeof v);
    return __builtin_bswap16(v);
  }
  static std::uint32_t u32(const std::uint8_t* p) noexcept {
    std::uint32_t v;
    std::memcpy(&v, p, sizeof v);
    return __builtin_bswap32(v);
  }
  static std::uint64_t u64(const std::uint8_t* p) noexcept {
    std::uint64_t v;
    std::memcpy(&v, p, sizeof v);
    return __builtin_bswap64(v);
  }
};

// ------------------------------------------------------------------ byte reader
// Bounds-checked big-endian cursor. Every read validates the remaining length,
// matching the safety semantics of the Rust and C# versions.
template <class Load>
struct Reader {
  const std::uint8_t* p;
  std::size_t len;
  std::size_t off = 0;

  [[nodiscard]] bool need(std::size_t n) const noexcept { return n <= len - off; }

  bool u8(std::uint8_t& out) noexcept {
    if (!need(1)) return false;
    out = p[off];
    off += 1;
    return true;
  }
  bool u16(std::uint16_t& out) noexcept {
    if (!need(2)) return false;
    out = Load::u16(p + off);
    off += 2;
    return true;
  }
  bool u32(std::uint32_t& out) noexcept {
    if (!need(4)) return false;
    out = Load::u32(p + off);
    off += 4;
    return true;
  }
  bool u64(std::uint64_t& out) noexcept {
    if (!need(8)) return false;
    out = Load::u64(p + off);
    off += 8;
    return true;
  }
  bool f32(float& out) noexcept {
    std::uint32_t v;
    if (!u32(v)) return false;
    out = std::bit_cast<float>(v);
    return true;
  }
  bool f64(double& out) noexcept {
    std::uint64_t v;
    if (!u64(v)) return false;
    out = std::bit_cast<double>(v);
    return true;
  }
  bool skip(std::size_t n) noexcept {
    if (!need(n)) return false;
    off += n;
    return true;
  }
};

// ------------------------------------------------------------------------ ESPDU
struct EntityState {
  std::uint16_t site, application, entity;
  std::uint8_t force_id;
  std::uint8_t kind, domain;
  std::uint16_t country;
  double location[3];
  float orientation[3];
  float velocity[3];
  std::uint8_t dr_algorithm;
  float lin_accel[3];
  float ang_vel[3];
  std::uint32_t appearance;
  std::uint32_t capabilities;
  char marking[11];
};

inline constexpr std::size_t kEspduSize = 144;
inline constexpr std::size_t kCount = 1024;  // 147,456 B corpus: stays cache-resident

// Inlining boundary: explicit and identical in every language and harness. Left to the
// optimizer, whether decode is inlined into the timed loop changed with the harness and the
// compiler (clang: never; gcc: always; Rust: only under Criterion), swinging results ~2x.
// The unit measured is one call, as in real per-datagram use.
template <class Load>
[[gnu::noinline]] bool decode_espdu(const std::uint8_t* buf, std::size_t len, EntityState& out) noexcept {
  Reader<Load> r{buf, len, 0};
  std::uint8_t version, exercise, pdu_type, family, pad8;
  std::uint32_t timestamp;
  std::uint16_t pdu_length;
  if (!r.u8(version) || !r.u8(exercise) || !r.u8(pdu_type) || !r.u8(family)) return false;
  if (!r.u32(timestamp) || !r.u16(pdu_length)) return false;
  if (!r.u8(pad8) || !r.u8(pad8)) return false;
  if (pdu_type != 1) return false;
  if (pdu_length < kEspduSize) return false;

  std::uint8_t art_count;
  if (!r.u16(out.site) || !r.u16(out.application) || !r.u16(out.entity)) return false;
  if (!r.u8(out.force_id) || !r.u8(art_count)) return false;

  std::uint8_t cat, sub, spec, extra;
  if (!r.u8(out.kind) || !r.u8(out.domain) || !r.u16(out.country)) return false;
  if (!r.u8(cat) || !r.u8(sub) || !r.u8(spec) || !r.u8(extra)) return false;
  if (!r.skip(8)) return false;  // alternative entity type

  for (int i = 0; i < 3; ++i)
    if (!r.f32(out.velocity[i])) return false;
  for (int i = 0; i < 3; ++i)
    if (!r.f64(out.location[i])) return false;
  for (int i = 0; i < 3; ++i)
    if (!r.f32(out.orientation[i])) return false;
  if (!r.u32(out.appearance)) return false;

  if (!r.u8(out.dr_algorithm)) return false;
  if (!r.skip(15)) return false;  // other DR parameters
  for (int i = 0; i < 3; ++i)
    if (!r.f32(out.lin_accel[i])) return false;
  for (int i = 0; i < 3; ++i)
    if (!r.f32(out.ang_vel[i])) return false;

  std::uint8_t charset;
  if (!r.u8(charset)) return false;
  for (int i = 0; i < 11; ++i) {
    std::uint8_t c;
    if (!r.u8(c)) return false;
    out.marking[i] = static_cast<char>(c);
  }
  if (!r.u32(out.capabilities)) return false;
  return true;
}

// --------------------------------------------------------------- dead reckoning
struct Mat3 {
  double m[9];
};

inline void mat_vec(const Mat3& a, const double v[3], double out[3]) noexcept {
  for (int i = 0; i < 3; ++i)
    out[i] = a.m[i * 3 + 0] * v[0] + a.m[i * 3 + 1] * v[1] + a.m[i * 3 + 2] * v[2];
}

inline Mat3 dcm_from_euler(double psi, double theta, double phi) noexcept {
  const double cps = std::cos(psi), sps = std::sin(psi);
  const double cth = std::cos(theta), sth = std::sin(theta);
  const double cph = std::cos(phi), sph = std::sin(phi);
  Mat3 r{};
  r.m[0] = cth * cps;
  r.m[1] = cth * sps;
  r.m[2] = -sth;
  r.m[3] = sph * sth * cps - cph * sps;
  r.m[4] = sph * sth * sps + cph * cps;
  r.m[5] = sph * cth;
  r.m[6] = cph * sth * cps + sph * sps;
  r.m[7] = cph * sth * sps - sph * cps;
  r.m[8] = cph * cth;
  return r;
}

// DRM_RVB: body-frame velocity and acceleration, rotating.
// P(t) = P0 + R0^T (R1 V0 + R2 A0)
// Not inlined, for the same reason as decode_espdu.
[[gnu::noinline]] inline void drm_rvb(const double p0[3], const double v0[3], const double a0[3], const double w[3],
                    double psi, double theta, double phi, double t, double out[3]) noexcept {
  const Mat3 r0 = dcm_from_euler(psi, theta, phi);
  const double w2 = w[0] * w[0] + w[1] * w[1] + w[2] * w[2];
  const double wm = std::sqrt(w2);

  Mat3 R1{}, R2{};
  if (wm < 1e-8) {
    // Series limits: R1 -> t*I, R2 -> (t^2/2)*I. Avoids the w^3 / w^4 divisions.
    for (int i = 0; i < 3; ++i) {
      R1.m[i * 3 + i] = t;
      R2.m[i * 3 + i] = 0.5 * t * t;
    }
  } else {
    const double wt = wm * t;
    const double s = std::sin(wt), c = std::cos(wt);
    const double w3 = w2 * wm, w4 = w2 * w2;

    const double c1_i = s / wm;
    const double c1_sk = (1.0 - c) / w2;
    const double c1_op = (wt - s) / w3;

    const double c2_i = (c + wt * s - 1.0) / w2;
    const double c2_sk = (s - wt * c) / w3;
    const double c2_op = (0.5 * w2 * t * t + c - 1.0) / w4;

    const double sk[9] = {0.0, -w[2], w[1], w[2], 0.0, -w[0], -w[1], w[0], 0.0};
    const double op[9] = {w[0] * w[0], w[0] * w[1], w[0] * w[2], w[1] * w[0], w[1] * w[1],
                          w[1] * w[2], w[2] * w[0], w[2] * w[1], w[2] * w[2]};
    for (int i = 0; i < 9; ++i) {
      const double id = (i % 4 == 0) ? 1.0 : 0.0;
      R1.m[i] = c1_i * id + c1_sk * sk[i] + c1_op * op[i];
      R2.m[i] = c2_i * id + c2_sk * sk[i] + c2_op * op[i];
    }
  }

  double tv[3], ta[3];
  mat_vec(R1, v0, tv);
  mat_vec(R2, a0, ta);
  const double sum[3] = {tv[0] + ta[0], tv[1] + ta[1], tv[2] + ta[2]};

  Mat3 r0t{};  // R0^T
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) r0t.m[i * 3 + j] = r0.m[j * 3 + i];

  double rot[3];
  mat_vec(r0t, sum, rot);
  out[0] = p0[0] + rot[0];
  out[1] = p0[1] + rot[1];
  out[2] = p0[2] + rot[2];
}

// ------------------------------------------------------------------ corpus + inputs
inline void put_u16(std::uint8_t* p, std::uint16_t v) {
  p[0] = static_cast<std::uint8_t>(v >> 8);
  p[1] = static_cast<std::uint8_t>(v);
}
inline void put_u32(std::uint8_t* p, std::uint32_t v) {
  p[0] = static_cast<std::uint8_t>(v >> 24);
  p[1] = static_cast<std::uint8_t>(v >> 16);
  p[2] = static_cast<std::uint8_t>(v >> 8);
  p[3] = static_cast<std::uint8_t>(v);
}
inline void put_f32(std::uint8_t* p, float f) { put_u32(p, std::bit_cast<std::uint32_t>(f)); }
inline void put_f64(std::uint8_t* p, double d) {
  const std::uint64_t v = std::bit_cast<std::uint64_t>(d);
  for (int i = 0; i < 8; ++i) p[i] = static_cast<std::uint8_t>(v >> (56 - 8 * i));
}

// Deterministic corpus: every language builds these exact bytes (checked by FNV-1a hash).
inline std::vector<std::uint8_t> build_corpus() {
  std::vector<std::uint8_t> buf(kCount * kEspduSize, 0);
  for (std::size_t i = 0; i < kCount; ++i) {
    std::uint8_t* p = buf.data() + i * kEspduSize;
    p[0] = 7;  // protocol version 1278.1-2012
    p[1] = 1;  // exercise
    p[2] = 1;  // pdu type: Entity State
    p[3] = 1;  // family
    put_u32(p + 4, static_cast<std::uint32_t>(i));
    put_u16(p + 8, static_cast<std::uint16_t>(kEspduSize));
    put_u16(p + 12, 1);
    put_u16(p + 14, 2);
    put_u16(p + 16, static_cast<std::uint16_t>(i));
    p[18] = 1;
    p[19] = 0;
    p[20] = 1;
    p[21] = 1;
    put_u16(p + 22, 225);
    for (int k = 0; k < 3; ++k) put_f32(p + 36 + 4 * k, 10.0f + static_cast<float>(k));
    for (int k = 0; k < 3; ++k) put_f64(p + 48 + 8 * k, 1.0e6 + static_cast<double>(i + k));
    for (int k = 0; k < 3; ++k) put_f32(p + 72 + 4 * k, 0.1f * static_cast<float>(k + 1));
    put_u32(p + 84, 0);
    p[88] = 8;
    for (int k = 0; k < 3; ++k) put_f32(p + 104 + 4 * k, 0.5f);
    for (int k = 0; k < 3; ++k) put_f32(p + 116 + 4 * k, 0.01f);
    p[128] = 1;
    for (int k = 0; k < 11; ++k) p[129 + k] = static_cast<std::uint8_t>('A' + (k % 26));
    put_u32(p + 140, 0);
  }
  return buf;
}

// Dead-reckoning inputs, flattened [entity*3 + axis].
struct DrInputs {
  std::vector<double> p0, v0, a0, w, eul;
};

inline constexpr double kDrDt = 0.05;

inline DrInputs build_dr_inputs() {
  DrInputs in;
  for (auto* v : {&in.p0, &in.v0, &in.a0, &in.w, &in.eul}) v->resize(kCount * 3);
  for (std::size_t i = 0; i < kCount; ++i) {
    const double fi = static_cast<double>(i);
    for (int k = 0; k < 3; ++k) {
      in.p0[i * 3 + k] = 1.0e6 + fi + k;
      in.v0[i * 3 + k] = 10.0 + 0.1 * k;
      in.a0[i * 3 + k] = 0.5 + 0.01 * k;
      in.w[i * 3 + k] = 0.01 * (k + 1) + 1.0e-4 * std::fmod(fi, 7.0);
      in.eul[i * 3 + k] = 0.1 * (k + 1);
    }
  }
  return in;
}

inline std::uint64_t fnv1a64(const std::uint8_t* p, std::size_t n) {
  std::uint64_t h = 0xcbf29ce484222325ULL;
  for (std::size_t i = 0; i < n; ++i) {
    h ^= p[i];
    h *= 0x100000001b3ULL;
  }
  return h;
}

// ------------------------------------------------------------------------- dump
// Canonical text rendering used for cross-language equivalence (scripts/verify.py).
// Floats are printed as IEEE-754 bit patterns so "equal" means bit-identical.
namespace detail {
inline unsigned bits(float f) { return std::bit_cast<std::uint32_t>(f); }
inline unsigned long long bits(double d) { return std::bit_cast<std::uint64_t>(d); }

template <class L>
void dump_variant(std::FILE* f, const std::vector<std::uint8_t>& c) {
  for (std::size_t i = 0; i < kCount; ++i) {
    EntityState e{};
    if (!decode_espdu<L>(c.data() + i * kEspduSize, kEspduSize, e)) {
      std::fprintf(f, "pdu %s %zu DECODE_FAILED\n", L::name, i);
      continue;
    }
    std::fprintf(f, "pdu %s %zu %u %u %u %u %u %u %u", L::name, i, e.site, e.application,
                 e.entity, e.force_id, e.kind, e.domain, e.country);
    for (float v : e.velocity) std::fprintf(f, " %08x", bits(v));
    for (double v : e.location) std::fprintf(f, " %016llx", bits(v));
    for (float v : e.orientation) std::fprintf(f, " %08x", bits(v));
    std::fprintf(f, " %08x %u", e.appearance, e.dr_algorithm);
    for (float v : e.lin_accel) std::fprintf(f, " %08x", bits(v));
    for (float v : e.ang_vel) std::fprintf(f, " %08x", bits(v));
    std::fputc(' ', f);
    for (char ch : e.marking) std::fprintf(f, "%02x", static_cast<unsigned char>(ch));
    std::fprintf(f, " %08x\n", e.capabilities);
  }

  EntityState e{};
  int truncated = 0;
  for (std::size_t n = 0; n < kEspduSize; ++n) truncated += decode_espdu<L>(c.data(), n, e);
  const int full = decode_espdu<L>(c.data(), kEspduSize, e);
  std::vector<std::uint8_t> bad(c.begin(), c.begin() + kEspduSize);
  bad[2] = 2;
  const int bad_type = decode_espdu<L>(bad.data(), kEspduSize, e);
  std::vector<std::uint8_t> short_len(c.begin(), c.begin() + kEspduSize);
  short_len[9] = 143;
  const int short_length = decode_espdu<L>(short_len.data(), kEspduSize, e);
  std::fprintf(f, "edge %s truncated_accepts %d\n", L::name, truncated);
  std::fprintf(f, "edge %s full_accepts %d\n", L::name, full);
  std::fprintf(f, "edge %s bad_type_accepts %d\n", L::name, bad_type);
  std::fprintf(f, "edge %s short_length_accepts %d\n", L::name, short_length);
}

inline void dump_dr_line(std::FILE* f, const char* prefix, const double out[3]) {
  std::fprintf(f, "%s %016llx %016llx %016llx\n", prefix, bits(out[0]), bits(out[1]),
               bits(out[2]));
}
}  // namespace detail

inline void write_dump(std::FILE* f) {
  const auto c = build_corpus();
  std::fprintf(f, "corpus_fnv1a64 %016llx\n",
               static_cast<unsigned long long>(fnv1a64(c.data(), c.size())));
  detail::dump_variant<LoadBswap>(f, c);
  detail::dump_variant<LoadShift>(f, c);

  const DrInputs in = build_dr_inputs();
  char prefix[32];
  double out[3];
  for (std::size_t i = 0; i < kCount; ++i) {
    drm_rvb(&in.p0[i * 3], &in.v0[i * 3], &in.a0[i * 3], &in.w[i * 3], in.eul[i * 3],
            in.eul[i * 3 + 1], in.eul[i * 3 + 2], kDrDt, out);
    std::snprintf(prefix, sizeof prefix, "dr %zu", i);
    detail::dump_dr_line(f, prefix, out);
  }
  // Displacement only (P0 = 0). The full outputs above are ~1e6 m, so a last-bit
  // difference in the ~0.5 m displacement would be rounded away by the final addition.
  const double origin[3] = {0.0, 0.0, 0.0};
  for (std::size_t i = 0; i < kCount; ++i) {
    drm_rvb(origin, &in.v0[i * 3], &in.a0[i * 3], &in.w[i * 3], in.eul[i * 3], in.eul[i * 3 + 1],
            in.eul[i * 3 + 2], kDrDt, out);
    std::snprintf(prefix, sizeof prefix, "dr_disp %zu", i);
    detail::dump_dr_line(f, prefix, out);
  }
  const double zero_w[3] = {0.0, 0.0, 0.0};
  drm_rvb(&in.p0[0], &in.v0[0], &in.a0[0], zero_w, in.eul[0], in.eul[1], in.eul[2], kDrDt, out);
  detail::dump_dr_line(f, "dr_zero", out);
}

}  // namespace dis
