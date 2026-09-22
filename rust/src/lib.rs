//! DIS benchmark kernels, Rust side.
//!   (1) Entity State PDU decode, with two byte-read idioms selectable by type parameter.
//!   (2) Body-frame dead reckoning, DRM_RVB (IEEE 1278.1 Annex E).
//! Shared by the dependency-free harness (src/main.rs) and the Criterion harness
//! (benches/kernels.rs), so both measure the same code. Parallel to cpp/include/kernels.hpp.

use std::io::{self, Write};

pub const ESPDU_SIZE: usize = 144;
pub const COUNT: usize = 1024; // 147,456 B corpus: stays cache-resident
pub const DR_DT: f64 = 0.05;

// ------------------------------------------------------------------ byte-read idioms

/// Big-endian primitive loads from fixed-size arrays (the bounds check happens before,
/// in `Reader::take`, so these cannot fail).
pub trait Load {
    const NAME: &'static str;
    fn u16(b: [u8; 2]) -> u16;
    fn u32(b: [u8; 4]) -> u32;
    fn u64(b: [u8; 8]) -> u64;
}

/// `from_be_bytes`: an intrinsic by construction. Idiomatic Rust cannot write the slow version.
pub struct Idiomatic;
impl Load for Idiomatic {
    const NAME: &'static str = "idiomatic";
    #[inline(always)]
    fn u16(b: [u8; 2]) -> u16 { u16::from_be_bytes(b) }
    #[inline(always)]
    fn u32(b: [u8; 4]) -> u32 { u32::from_be_bytes(b) }
    #[inline(always)]
    fn u64(b: [u8; 8]) -> u64 { u64::from_be_bytes(b) }
}

/// Hand-rolled shifts, transliterated from the C++ `LoadShift`.
pub struct Shift;
impl Load for Shift {
    const NAME: &'static str = "shift";
    #[inline(always)]
    fn u16(b: [u8; 2]) -> u16 { ((b[0] as u16) << 8) | b[1] as u16 }
    #[inline(always)]
    fn u32(b: [u8; 4]) -> u32 {
        ((b[0] as u32) << 24) | ((b[1] as u32) << 16) | ((b[2] as u32) << 8) | b[3] as u32
    }
    #[inline(always)]
    fn u64(b: [u8; 8]) -> u64 {
        let mut v = 0u64;
        for x in b {
            v = (v << 8) | x as u64;
        }
        v
    }
}

// ------------------------------------------------------------------------ byte reader

/// Bounds-checked big-endian cursor. `slice::get` returns `None` past the end.
struct Reader<'a, L: Load> {
    buf: &'a [u8],
    off: usize,
    _idiom: std::marker::PhantomData<L>,
}

impl<'a, L: Load> Reader<'a, L> {
    #[inline(always)]
    fn new(buf: &'a [u8]) -> Self { Reader { buf, off: 0, _idiom: std::marker::PhantomData } }
    #[inline(always)]
    fn take<const N: usize>(&mut self) -> Option<[u8; N]> {
        let s = self.buf.get(self.off..self.off.checked_add(N)?)?;
        self.off += N;
        Some(s.try_into().unwrap())
    }
    #[inline(always)]
    fn u8(&mut self) -> Option<u8> { Some(self.take::<1>()?[0]) }
    #[inline(always)]
    fn u16(&mut self) -> Option<u16> { Some(L::u16(self.take()?)) }
    #[inline(always)]
    fn u32(&mut self) -> Option<u32> { Some(L::u32(self.take()?)) }
    #[inline(always)]
    fn u64(&mut self) -> Option<u64> { Some(L::u64(self.take()?)) }
    #[inline(always)]
    fn f32(&mut self) -> Option<f32> { Some(f32::from_bits(self.u32()?)) }
    #[inline(always)]
    fn f64(&mut self) -> Option<f64> { Some(f64::from_bits(self.u64()?)) }
    #[inline(always)]
    fn skip(&mut self, n: usize) -> Option<()> {
        let end = self.off.checked_add(n)?;
        if end > self.buf.len() {
            return None;
        }
        self.off = end;
        Some(())
    }
}

// ------------------------------------------------------------------------------ ESPDU

#[derive(Default, Clone, Copy, Debug, PartialEq)]
pub struct EntityState {
    pub site: u16,
    pub application: u16,
    pub entity: u16,
    pub force_id: u8,
    pub kind: u8,
    pub domain: u8,
    pub country: u16,
    pub location: [f64; 3],
    pub orientation: [f32; 3],
    pub velocity: [f32; 3],
    pub dr_algorithm: u8,
    pub lin_accel: [f32; 3],
    pub ang_vel: [f32; 3],
    pub appearance: u32,
    pub capabilities: u32,
    pub marking: [u8; 11],
}

/// Inlining boundary: explicit and identical in every language and harness. Left to the
/// optimizer, whether decode is inlined into the timed loop changed with the harness and the
/// compiler (clang: never; gcc: always; Rust: only under Criterion), swinging results ~2x.
/// The unit measured is one call, as in real per-datagram use.
#[inline(never)]
pub fn decode_espdu<L: Load>(buf: &[u8]) -> Option<EntityState> {
    let mut r = Reader::<L>::new(buf);
    let _version = r.u8()?;
    let _exercise = r.u8()?;
    let pdu_type = r.u8()?;
    let _family = r.u8()?;
    let _timestamp = r.u32()?;
    let pdu_length = r.u16()?;
    let _pad = (r.u8()?, r.u8()?);
    if pdu_type != 1 || (pdu_length as usize) < ESPDU_SIZE {
        return None;
    }

    let mut out = EntityState {
        site: r.u16()?,
        application: r.u16()?,
        entity: r.u16()?,
        force_id: r.u8()?,
        ..Default::default()
    };
    let _art_count = r.u8()?;
    out.kind = r.u8()?;
    out.domain = r.u8()?;
    out.country = r.u16()?;
    let _category = (r.u8()?, r.u8()?, r.u8()?, r.u8()?); // category, subcat, specific, extra
    r.skip(8)?; // alternative entity type
    for k in 0..3 {
        out.velocity[k] = r.f32()?;
    }
    for k in 0..3 {
        out.location[k] = r.f64()?;
    }
    for k in 0..3 {
        out.orientation[k] = r.f32()?;
    }
    out.appearance = r.u32()?;
    out.dr_algorithm = r.u8()?;
    r.skip(15)?; // other DR parameters
    for k in 0..3 {
        out.lin_accel[k] = r.f32()?;
    }
    for k in 0..3 {
        out.ang_vel[k] = r.f32()?;
    }
    let _charset = r.u8()?;
    for k in 0..11 {
        out.marking[k] = r.u8()?;
    }
    out.capabilities = r.u32()?;
    Some(out)
}

// --------------------------------------------------------------------- dead reckoning

type Mat3 = [f64; 9];

#[inline(always)]
fn mat_vec(a: &Mat3, v: &[f64; 3]) -> [f64; 3] {
    let mut out = [0.0; 3];
    for i in 0..3 {
        out[i] = a[i * 3] * v[0] + a[i * 3 + 1] * v[1] + a[i * 3 + 2] * v[2];
    }
    out
}

#[inline(always)]
fn dcm_from_euler(psi: f64, theta: f64, phi: f64) -> Mat3 {
    let (cps, sps) = (psi.cos(), psi.sin());
    let (cth, sth) = (theta.cos(), theta.sin());
    let (cph, sph) = (phi.cos(), phi.sin());
    [
        cth * cps,
        cth * sps,
        -sth,
        sph * sth * cps - cph * sps,
        sph * sth * sps + cph * cps,
        sph * cth,
        cph * sth * cps + sph * sps,
        cph * sth * sps - sph * cps,
        cph * cth,
    ]
}

/// DRM_RVB: body-frame velocity and acceleration, rotating.
/// P(t) = P0 + R0^T (R1 V0 + R2 A0)
/// Not inlined, for the same reason as `decode_espdu`.
#[inline(never)]
#[allow(clippy::too_many_arguments)]
pub fn drm_rvb(
    p0: &[f64; 3], v0: &[f64; 3], a0: &[f64; 3], w: &[f64; 3],
    psi: f64, theta: f64, phi: f64, t: f64,
) -> [f64; 3] {
    let r0 = dcm_from_euler(psi, theta, phi);
    let w2 = w[0] * w[0] + w[1] * w[1] + w[2] * w[2];
    let wm = w2.sqrt();

    let mut r1: Mat3 = [0.0; 9];
    let mut r2: Mat3 = [0.0; 9];
    if wm < 1e-8 {
        // Series limits: R1 -> t*I, R2 -> (t^2/2)*I. Avoids the w^3 / w^4 divisions.
        for i in 0..3 {
            r1[i * 3 + i] = t;
            r2[i * 3 + i] = 0.5 * t * t;
        }
    } else {
        let wt = wm * t;
        let (s, c) = (wt.sin(), wt.cos());
        let (w3, w4) = (w2 * wm, w2 * w2);

        let c1_i = s / wm;
        let c1_sk = (1.0 - c) / w2;
        let c1_op = (wt - s) / w3;

        let c2_i = (c + wt * s - 1.0) / w2;
        let c2_sk = (s - wt * c) / w3;
        let c2_op = (0.5 * w2 * t * t + c - 1.0) / w4;

        let sk = [0.0, -w[2], w[1], w[2], 0.0, -w[0], -w[1], w[0], 0.0];
        let op = [
            w[0] * w[0], w[0] * w[1], w[0] * w[2],
            w[1] * w[0], w[1] * w[1], w[1] * w[2],
            w[2] * w[0], w[2] * w[1], w[2] * w[2],
        ];
        for i in 0..9 {
            let id = if i % 4 == 0 { 1.0 } else { 0.0 };
            r1[i] = c1_i * id + c1_sk * sk[i] + c1_op * op[i];
            r2[i] = c2_i * id + c2_sk * sk[i] + c2_op * op[i];
        }
    }

    let tv = mat_vec(&r1, v0);
    let ta = mat_vec(&r2, a0);
    let sum = [tv[0] + ta[0], tv[1] + ta[1], tv[2] + ta[2]];

    let mut r0t: Mat3 = [0.0; 9]; // R0^T
    for i in 0..3 {
        for j in 0..3 {
            r0t[i * 3 + j] = r0[j * 3 + i];
        }
    }
    let rot = mat_vec(&r0t, &sum);
    [p0[0] + rot[0], p0[1] + rot[1], p0[2] + rot[2]]
}

// ------------------------------------------------------------------- corpus + inputs

/// Deterministic corpus: every language builds these exact bytes (checked by FNV-1a hash).
pub fn build_corpus() -> Vec<u8> {
    let mut buf = vec![0u8; COUNT * ESPDU_SIZE];
    for (i, p) in buf.chunks_exact_mut(ESPDU_SIZE).enumerate() {
        p[0] = 7; // protocol version 1278.1-2012
        p[1] = 1; // exercise
        p[2] = 1; // pdu type: Entity State
        p[3] = 1; // family
        p[4..8].copy_from_slice(&(i as u32).to_be_bytes());
        p[8..10].copy_from_slice(&(ESPDU_SIZE as u16).to_be_bytes());
        p[12..14].copy_from_slice(&1u16.to_be_bytes());
        p[14..16].copy_from_slice(&2u16.to_be_bytes());
        p[16..18].copy_from_slice(&(i as u16).to_be_bytes());
        p[18] = 1;
        p[19] = 0;
        p[20] = 1;
        p[21] = 1;
        p[22..24].copy_from_slice(&225u16.to_be_bytes());
        for k in 0..3 {
            p[36 + 4 * k..40 + 4 * k].copy_from_slice(&(10.0f32 + k as f32).to_be_bytes());
            p[48 + 8 * k..56 + 8 * k].copy_from_slice(&(1.0e6 + (i + k) as f64).to_be_bytes());
            p[72 + 4 * k..76 + 4 * k].copy_from_slice(&(0.1f32 * (k + 1) as f32).to_be_bytes());
            p[104 + 4 * k..108 + 4 * k].copy_from_slice(&0.5f32.to_be_bytes());
            p[116 + 4 * k..120 + 4 * k].copy_from_slice(&0.01f32.to_be_bytes());
        }
        p[84..88].copy_from_slice(&0u32.to_be_bytes());
        p[88] = 8;
        p[128] = 1;
        for k in 0..11 {
            p[129 + k] = b'A' + (k % 26) as u8;
        }
        p[140..144].copy_from_slice(&0u32.to_be_bytes());
    }
    buf
}

/// Dead-reckoning inputs, one fixed-size array per entity.
pub struct DrInputs {
    pub p0: Vec<[f64; 3]>,
    pub v0: Vec<[f64; 3]>,
    pub a0: Vec<[f64; 3]>,
    pub w: Vec<[f64; 3]>,
    pub eul: Vec<[f64; 3]>,
}

pub fn build_dr_inputs() -> DrInputs {
    let mut d = DrInputs {
        p0: vec![[0.0; 3]; COUNT],
        v0: vec![[0.0; 3]; COUNT],
        a0: vec![[0.0; 3]; COUNT],
        w: vec![[0.0; 3]; COUNT],
        eul: vec![[0.0; 3]; COUNT],
    };
    for i in 0..COUNT {
        let fi = i as f64;
        for k in 0..3 {
            let kf = k as f64;
            d.p0[i][k] = 1.0e6 + fi + kf;
            d.v0[i][k] = 10.0 + 0.1 * kf;
            d.a0[i][k] = 0.5 + 0.01 * kf;
            d.w[i][k] = 0.01 * (kf + 1.0) + 1.0e-4 * (fi % 7.0);
            d.eul[i][k] = 0.1 * (kf + 1.0);
        }
    }
    d
}

pub fn fnv1a64(b: &[u8]) -> u64 {
    let mut h: u64 = 0xcbf29ce484222325;
    for &x in b {
        h ^= x as u64;
        h = h.wrapping_mul(0x100000001b3);
    }
    h
}

// ------------------------------------------------------------------------------- dump

/// Canonical text rendering used for cross-language equivalence (scripts/verify.py).
/// Floats are printed as IEEE-754 bit patterns so "equal" means bit-identical.
pub fn write_dump(w: &mut impl Write) -> io::Result<()> {
    let c = build_corpus();
    writeln!(w, "corpus_fnv1a64 {:016x}", fnv1a64(&c))?;
    dump_variant::<Idiomatic>(w, &c)?;
    dump_variant::<Shift>(w, &c)?;

    let d = build_dr_inputs();
    for i in 0..COUNT {
        let [psi, theta, phi] = d.eul[i];
        let out = drm_rvb(&d.p0[i], &d.v0[i], &d.a0[i], &d.w[i], psi, theta, phi, DR_DT);
        writeln!(w, "dr {i} {:016x} {:016x} {:016x}", out[0].to_bits(), out[1].to_bits(), out[2].to_bits())?;
    }
    // Displacement only (P0 = 0). The full outputs above are ~1e6 m, so a last-bit
    // difference in the ~0.5 m displacement would be rounded away by the final addition.
    for i in 0..COUNT {
        let [psi, theta, phi] = d.eul[i];
        let out = drm_rvb(&[0.0; 3], &d.v0[i], &d.a0[i], &d.w[i], psi, theta, phi, DR_DT);
        writeln!(w, "dr_disp {i} {:016x} {:016x} {:016x}", out[0].to_bits(), out[1].to_bits(), out[2].to_bits())?;
    }
    let [psi, theta, phi] = d.eul[0];
    let out = drm_rvb(&d.p0[0], &d.v0[0], &d.a0[0], &[0.0; 3], psi, theta, phi, DR_DT);
    writeln!(w, "dr_zero {:016x} {:016x} {:016x}", out[0].to_bits(), out[1].to_bits(), out[2].to_bits())
}

fn dump_variant<L: Load>(w: &mut impl Write, c: &[u8]) -> io::Result<()> {
    let name = L::NAME;
    for (i, pdu) in c.chunks_exact(ESPDU_SIZE).enumerate() {
        let Some(e) = decode_espdu::<L>(pdu) else {
            writeln!(w, "pdu {name} {i} DECODE_FAILED")?;
            continue;
        };
        write!(w, "pdu {name} {i} {} {} {} {} {} {} {}", e.site, e.application, e.entity,
               e.force_id, e.kind, e.domain, e.country)?;
        for v in e.velocity { write!(w, " {:08x}", v.to_bits())?; }
        for v in e.location { write!(w, " {:016x}", v.to_bits())?; }
        for v in e.orientation { write!(w, " {:08x}", v.to_bits())?; }
        write!(w, " {:08x} {}", e.appearance, e.dr_algorithm)?;
        for v in e.lin_accel { write!(w, " {:08x}", v.to_bits())?; }
        for v in e.ang_vel { write!(w, " {:08x}", v.to_bits())?; }
        write!(w, " ")?;
        for ch in e.marking { write!(w, "{ch:02x}")?; }
        writeln!(w, " {:08x}", e.capabilities)?;
    }

    let full = &c[..ESPDU_SIZE];
    let truncated = (0..ESPDU_SIZE).filter(|&n| decode_espdu::<L>(&full[..n]).is_some()).count();
    let full_ok = decode_espdu::<L>(full).is_some() as u32;
    let mut bad = full.to_vec();
    bad[2] = 2;
    let bad_type = decode_espdu::<L>(&bad).is_some() as u32;
    let mut short_len = full.to_vec();
    short_len[9] = 143;
    let short_length = decode_espdu::<L>(&short_len).is_some() as u32;
    writeln!(w, "edge {name} truncated_accepts {truncated}")?;
    writeln!(w, "edge {name} full_accepts {full_ok}")?;
    writeln!(w, "edge {name} bad_type_accepts {bad_type}")?;
    writeln!(w, "edge {name} short_length_accepts {short_length}")
}

#[cfg(test)]
mod tests {
    use super::*;

    fn known_fields<L: Load>() {
        let c = build_corpus();
        let es = decode_espdu::<L>(&c[..ESPDU_SIZE]).expect("decodes");
        assert_eq!((es.site, es.application, es.entity), (1, 2, 0));
        assert_eq!((es.force_id, es.kind, es.domain, es.country), (1, 1, 1, 225));
        assert_eq!(es.velocity[0], 10.0);
        assert_eq!(es.velocity[2], 12.0);
        assert_eq!(es.location[0], 1.0e6);
        assert_eq!(es.location[2], 1.0e6 + 2.0);
        assert_eq!(es.orientation[1], 0.2f32);
        assert_eq!(es.dr_algorithm, 8);
        assert_eq!(es.lin_accel[1], 0.5);
        assert_eq!(es.ang_vel[2], 0.01f32);
        assert_eq!(&es.marking, b"ABCDEFGHIJK");
    }

    fn every_pdu_has_its_index<L: Load>() {
        let c = build_corpus();
        for (i, pdu) in c.chunks_exact(ESPDU_SIZE).enumerate() {
            assert_eq!(decode_espdu::<L>(pdu).expect("decodes").entity, i as u16);
        }
    }

    fn rejects_malformed<L: Load>() {
        let c = build_corpus();
        for n in 0..ESPDU_SIZE {
            assert!(decode_espdu::<L>(&c[..n]).is_none(), "accepted truncated len {n}");
        }
        let mut bad_type = c[..ESPDU_SIZE].to_vec();
        bad_type[2] = 2;
        assert!(decode_espdu::<L>(&bad_type).is_none());
        let mut short_len = c[..ESPDU_SIZE].to_vec();
        short_len[9] = 143;
        assert!(decode_espdu::<L>(&short_len).is_none());
    }

    #[test] fn idiomatic_known_fields() { known_fields::<Idiomatic>() }
    #[test] fn shift_known_fields() { known_fields::<Shift>() }
    #[test] fn idiomatic_every_pdu() { every_pdu_has_its_index::<Idiomatic>() }
    #[test] fn shift_every_pdu() { every_pdu_has_its_index::<Shift>() }
    #[test] fn idiomatic_rejects_malformed() { rejects_malformed::<Idiomatic>() }
    #[test] fn shift_rejects_malformed() { rejects_malformed::<Shift>() }

    #[test]
    fn idioms_agree() {
        let c = build_corpus();
        for pdu in c.chunks_exact(ESPDU_SIZE) {
            assert_eq!(decode_espdu::<Idiomatic>(pdu), decode_espdu::<Shift>(pdu));
        }
    }

    #[test]
    fn dr_zero_omega_is_kinematic() {
        let out = drm_rvb(&[1.0e6, 2.0e6, 3.0e6], &[1.0, 0.0, 0.0], &[0.0; 3], &[0.0; 3], 0.0, 0.0, 0.0, 2.0);
        assert_eq!(out, [1.0e6 + 2.0, 2.0e6, 3.0e6]);
    }

    #[test]
    fn corpus_size_and_hash_function() {
        assert_eq!(build_corpus().len(), COUNT * ESPDU_SIZE);
        assert_eq!(fnv1a64(b"a"), 0xaf63dc4c8601ec8c);
    }
}
