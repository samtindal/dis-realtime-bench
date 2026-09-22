//! Dependency-free harness, Rust side. Parallel in structure to cpp/bench.cpp.
//!
//!   bench run [--toolchain LABEL] [--round N] [--reps 25] [--warmup-ms 500]
//!   bench dump

use std::hint::black_box;
use std::io::{self, BufWriter, Write};
use std::process::ExitCode;
use std::time::{Duration, Instant};

use dis_bench::{
    build_corpus, build_dr_inputs, decode_espdu, drm_rvb, write_dump, DrInputs, Idiomatic, Load,
    Shift, COUNT, DR_DT, ESPDU_SIZE,
};

/// One batch = the whole 1024-PDU corpus. Returns how many PDUs decoded, for the self-check.
fn decode_batch<L: Load>(corpus: &[u8], acc: &mut f64) -> usize {
    let mut decoded = 0;
    let mut local = 0.0;
    for pdu in corpus.chunks_exact(ESPDU_SIZE) {
        // black_box on the slice hides pointer AND length: bounds checks stay real.
        if let Some(es) = decode_espdu::<L>(black_box(pdu)) {
            // Barrier on the WHOLE struct. Barriering only the two fields read below let
            // LLVM delete the other ~20 field decodes (the 1.8 ns/PDU bug).
            let es = black_box(es);
            local += es.location[0] + es.entity as f64;
            decoded += 1;
        }
    }
    *acc += black_box(local);
    decoded
}

fn dr_batch(d: &DrInputs, acc: &mut f64) -> usize {
    let d = black_box(d);
    let mut local = 0.0;
    for i in 0..COUNT {
        let [psi, theta, phi] = d.eul[i];
        let out = drm_rvb(&d.p0[i], &d.v0[i], &d.a0[i], &d.w[i], psi, theta, phi, DR_DT);
        local += out[0] + out[1] + out[2];
    }
    *acc += black_box(local);
    COUNT
}

struct Options {
    toolchain: String,
    round: u32,
    reps: u32,
    warmup_ms: u64,
}

fn measure(o: &Options, out: &mut impl Write, kernel: &str, variant: &str, mut batch: impl FnMut() -> usize) {
    // Warmup: at least 5 batches AND at least warmup_ms, identical rule in every language.
    let w0 = Instant::now();
    let mut n = 0;
    while n < 5 || w0.elapsed() < Duration::from_millis(o.warmup_ms) {
        batch();
        n += 1;
    }
    for rep in 0..o.reps {
        let t0 = Instant::now();
        let done = batch();
        let ns = t0.elapsed().as_nanos() as f64;
        if done != COUNT {
            eprintln!("self-check failed: {kernel}/{variant} processed {done} of {COUNT}");
            std::process::exit(2);
        }
        writeln!(out, "simple,rust,{},{kernel},{variant},{},{rep},{:.4}", o.toolchain, o.round, ns / COUNT as f64)
            .unwrap();
    }
}

fn cmd_run(o: &Options) {
    let corpus = build_corpus();
    let dr_in = build_dr_inputs();
    let mut acc = 0.0;
    let mut out = io::stdout().lock();
    measure(o, &mut out, "decode", Idiomatic::NAME, || decode_batch::<Idiomatic>(&corpus, &mut acc));
    measure(o, &mut out, "decode", Shift::NAME, || decode_batch::<Shift>(&corpus, &mut acc));
    measure(o, &mut out, "dr", "default", || dr_batch(&dr_in, &mut acc));
    eprintln!("checksum {acc:.6e}");
}

fn usage() -> ExitCode {
    eprintln!("usage: bench run [--toolchain L] [--round N] [--reps N] [--warmup-ms N]\n       bench dump");
    ExitCode::from(64)
}

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    match args.first().map(String::as_str) {
        Some("dump") => {
            let mut w = BufWriter::new(io::stdout().lock());
            write_dump(&mut w).and_then(|_| w.flush()).expect("write dump");
            ExitCode::SUCCESS
        }
        Some("run") => {
            let mut o = Options { toolchain: env!("DIS_TOOLCHAIN").into(), round: 0, reps: 25, warmup_ms: 500 };
            let mut it = args[1..].iter();
            while let Some(flag) = it.next() {
                let Some(v) = it.next() else { return usage() };
                match flag.as_str() {
                    "--toolchain" => o.toolchain = v.clone(),
                    "--round" => o.round = v.parse().unwrap_or(0),
                    "--reps" => o.reps = v.parse().unwrap_or(25),
                    "--warmup-ms" => o.warmup_ms = v.parse().unwrap_or(500),
                    _ => return usage(),
                }
            }
            cmd_run(&o);
            ExitCode::SUCCESS
        }
        _ => usage(),
    }
}
