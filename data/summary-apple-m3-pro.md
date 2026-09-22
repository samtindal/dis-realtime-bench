Clock for cycles/op: 4.020 GHz (scripts: bench calibrate).

### decode / idiomatic (ns per op)

| harness | lang | toolchain | median | p25–p75 | min | cycles/op | n |
|---|---|---|---:|---:|---:|---:|---:|
| simple | cpp | apple-clang-21.0.0 | 11.15 | 11.11–11.88 | 11.03 | 44.8 | 2000 |
| gbench | cpp | gcc-16.2.0 | 11.46 | 11.46–11.50 | 11.44 | 46.1 | 20 |
| gbench | cpp | apple-clang-21.0.0 | 11.49 | 11.44–11.59 | 11.41 | 46.2 | 20 |
| simple | cpp | gcc-16.2.0 | 11.72 | 11.72–11.72 | 10.74 | 47.1 | 2000 |
| simple | rust | rustc-1.95.0 | 13.22 | 12.45–13.27 | 12.29 | 53.2 | 2000 |
| criterion | rust | rustc-1.95.0 | 13.31 | 13.28–13.34 | 13.02 | 53.5 | 100 |
| simple | csharp | dotnet-10.0.12-jit | 21.19 | 21.19–21.29 | 21.00 | 85.2 | 2000 |
| bdn | csharp | dotnet-10.0.12-jit | 21.23 | 21.21–21.25 | 21.18 | 85.3 | 14 |
| simple | csharp | dotnet-10.0.12-aot | 25.49 | 25.39–25.59 | 25.10 | 102.5 | 2000 |

### decode / shift (ns per op)

| harness | lang | toolchain | median | p25–p75 | min | cycles/op | n |
|---|---|---|---:|---:|---:|---:|---:|
| gbench | cpp | gcc-16.2.0 | 12.31 | 12.26–12.44 | 12.24 | 49.5 | 20 |
| simple | cpp | gcc-16.2.0 | 12.70 | 12.70–12.70 | 11.72 | 51.0 | 2000 |
| simple | rust | rustc-1.95.0 | 13.26 | 13.22–13.31 | 12.29 | 53.3 | 2000 |
| criterion | rust | rustc-1.95.0 | 13.31 | 13.28–13.34 | 12.91 | 53.5 | 100 |
| simple | cpp | apple-clang-21.0.0 | 16.44 | 16.28–16.60 | 16.07 | 66.1 | 2000 |
| gbench | cpp | apple-clang-21.0.0 | 18.07 | 17.86–18.09 | 17.72 | 72.7 | 20 |
| bdn | csharp | dotnet-10.0.12-jit | 28.80 | 28.72–29.04 | 28.66 | 115.8 | 13 |
| simple | csharp | dotnet-10.0.12-jit | 29.88 | 29.00–30.57 | 28.32 | 120.1 | 2000 |
| simple | csharp | dotnet-10.0.12-aot | 31.25 | 30.57–31.54 | 30.47 | 125.6 | 2000 |

### dr / default (ns per op)

| harness | lang | toolchain | median | p25–p75 | min | cycles/op | n |
|---|---|---|---:|---:|---:|---:|---:|
| criterion | rust | rustc-1.95.0 | 27.88 | 27.80–27.98 | 27.59 | 112.1 | 100 |
| simple | rust | rustc-1.95.0 | 28.04 | 27.91–28.16 | 27.59 | 112.7 | 2000 |
| simple | cpp | apple-clang-21.0.0 | 28.52 | 28.36–28.65 | 27.95 | 114.7 | 2000 |
| gbench | cpp | apple-clang-21.0.0 | 28.70 | 28.68–28.72 | 28.61 | 115.4 | 20 |
| simple | csharp | dotnet-10.0.12-jit | 49.02 | 48.93–49.80 | 45.70 | 197.1 | 2000 |
| bdn | csharp | dotnet-10.0.12-jit | 49.35 | 49.25–49.38 | 49.20 | 198.4 | 13 |
| simple | cpp | gcc-16.2.0 | 53.71 | 52.73–53.71 | 48.83 | 215.9 | 2000 |
| gbench | cpp | gcc-16.2.0 | 54.01 | 53.77–54.27 | 53.53 | 217.1 | 20 |
| simple | csharp | dotnet-10.0.12-aot | 58.59 | 58.50–58.79 | 58.11 | 235.5 | 2000 |

### Harness agreement (ecosystem median / dependency-free median)

| lang | toolchain | kernel | harness | simple | ecosystem | ratio |
|---|---|---|---|---:|---:|---:|
| csharp | dotnet-10.0.12-jit | decode/idiomatic | bdn | 21.19 | 21.23 | 1.00 |
| rust | rustc-1.95.0 | decode/idiomatic | criterion | 13.22 | 13.31 | 1.01 |
| cpp | apple-clang-21.0.0 | decode/idiomatic | gbench | 11.15 | 11.49 | 1.03 |
| cpp | gcc-16.2.0 | decode/idiomatic | gbench | 11.72 | 11.46 | 0.98 |
| csharp | dotnet-10.0.12-jit | decode/shift | bdn | 29.88 | 28.80 | 0.96 |
| rust | rustc-1.95.0 | decode/shift | criterion | 13.26 | 13.31 | 1.00 |
| cpp | apple-clang-21.0.0 | decode/shift | gbench | 16.44 | 18.07 | 1.10 |
| cpp | gcc-16.2.0 | decode/shift | gbench | 12.70 | 12.31 | 0.97 |
| csharp | dotnet-10.0.12-jit | dr/default | bdn | 49.02 | 49.35 | 1.01 |
| rust | rustc-1.95.0 | dr/default | criterion | 28.04 | 27.88 | 0.99 |
| cpp | apple-clang-21.0.0 | dr/default | gbench | 28.52 | 28.70 | 1.01 |
| cpp | gcc-16.2.0 | dr/default | gbench | 53.71 | 54.01 | 1.01 |

### Allocation (BenchmarkDotNet MemoryDiagnoser)

| lang | toolchain | kernel | variant | bytes/op | Gen0 |
|---|---|---|---|---:|---:|
| csharp | dotnet-10.0.12-jit | decode | idiomatic | 0 | 0 |
| csharp | dotnet-10.0.12-jit | decode | shift | 0 | 0 |
| csharp | dotnet-10.0.12-jit | dr | default | 0 | 0 |
