// Records the compiler version as the default toolchain label for the harness output.
use std::process::Command;

fn main() {
    let rustc = std::env::var("RUSTC").unwrap_or_else(|_| "rustc".into());
    let out = Command::new(rustc).arg("--version").output().expect("rustc --version");
    let text = String::from_utf8_lossy(&out.stdout);
    let version = text.split_whitespace().nth(1).unwrap_or("unknown");
    println!("cargo:rustc-env=DIS_TOOLCHAIN=rustc-{version}");
    println!("cargo:rerun-if-changed=build.rs");
}
