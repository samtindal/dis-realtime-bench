| binary | function | insns | cond branches | rev | loads | stores | fdiv | fmadd | calls |
|---|---|---:|---:|---:|---:|---:|---:|---:|---|
| clang | `decode_espdu<LoadBswap>` | 289 | 45 | 22 | 38 | 36 | 0 | 0 | — |
| clang | `decode_espdu<LoadShift>` | 305 | 46 | 19 | 66 | 59 | 0 | 0 | Reader<LoadShift>::f64×3 |
| clang | `drm_rvb` | 256 | 1 | 0 | 33 | 34 | 6 | 0 | sincos_stret×4 |
| gcc | `decode_espdu<LoadBswap>` | 143 | 27 | 14 | 20 | 18 | 0 | 0 | — |
| gcc | `decode_espdu<LoadBswap> (.constprop.0)` | 105 | 1 | 22 | 38 | 36 | 0 | 0 | — |
| gcc | `decode_espdu<LoadShift>` | 160 | 27 | 13 | 27 | 18 | 0 | 0 | — |
| gcc | `decode_espdu<LoadShift> (.constprop.0)` | 145 | 1 | 19 | 59 | 36 | 0 | 0 | — |
| gcc | `drm_rvb (.constprop.0)` | 258 | 1 | 0 | 35 | 39 | 6 | 0 | cexp×4 |
| rust | `decode_espdu [543661]` | 246 | 37 | 22 | 31 | 63 | 0 | 0 | — |
| rust | `decode_espdu [7bf7ac]` | 246 | 37 | 22 | 31 | 63 | 0 | 0 | — |
| rust | `drm_rvb [10df7f]` | 263 | 1 | 0 | 34 | 33 | 6 | 0 | sincos_stret×4 |
