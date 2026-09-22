//! Criterion harness, Rust side. Same kernels and batch shape as src/main.rs: one iteration =
//! one 1024-op batch, so ns/op = sample time / iters / 1024 (scripts/ingest.py does the division).

use std::hint::black_box;

use criterion::{criterion_group, criterion_main, Criterion, Throughput};
use dis_bench::{
    build_corpus, build_dr_inputs, decode_espdu, drm_rvb, Idiomatic, Load, Shift, COUNT, DR_DT, ESPDU_SIZE,
};

fn decode_batch<L: Load>(corpus: &[u8]) -> usize {
    let mut decoded = 0;
    for pdu in corpus.chunks_exact(ESPDU_SIZE) {
        // black_box on the slice (pointer + length) and on the whole decoded struct.
        if let Some(es) = decode_espdu::<L>(black_box(pdu)) {
            black_box(es);
            decoded += 1;
        }
    }
    assert_eq!(decoded, COUNT, "self-check failed: not every PDU decoded");
    decoded
}

fn kernels(c: &mut Criterion) {
    let corpus = build_corpus();
    let d = build_dr_inputs();

    let mut g = c.benchmark_group("decode");
    g.throughput(Throughput::Elements(COUNT as u64));
    g.bench_function(Idiomatic::NAME, |b| b.iter(|| decode_batch::<Idiomatic>(&corpus)));
    g.bench_function(Shift::NAME, |b| b.iter(|| decode_batch::<Shift>(&corpus)));
    g.finish();

    let mut g = c.benchmark_group("dr");
    g.throughput(Throughput::Elements(COUNT as u64));
    g.bench_function("default", |b| {
        b.iter(|| {
            let d = black_box(&d);
            let mut local = 0.0;
            for i in 0..COUNT {
                let [psi, theta, phi] = d.eul[i];
                let out = drm_rvb(&d.p0[i], &d.v0[i], &d.a0[i], &d.w[i], psi, theta, phi, DR_DT);
                local += out[0] + out[1] + out[2];
            }
            black_box(local)
        })
    });
    g.finish();
}

criterion_group!(benches, kernels);
criterion_main!(benches);
