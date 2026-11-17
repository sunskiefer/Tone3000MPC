# Vendored third-party code

Everything in `third_party/` belongs to its authors and stays under their licences (texts in `third_party/licenses/`).
THING4000's own code (`src/`, `tools/`, `layout.conf`, art) is PolyForm Noncommercial 1.0.0 (`LICENSE`).

| Code | Where | Version | Licence | Changes |
|---|---|---|---|---|
| NeuralAmpModelerCore (the NAM engine, A2 included) | `third_party/NeuralAmpModelerCore/` | commit 1f42f88535884450104b8711d7595019afa0495b | MIT, Steven Atkinson and contributors | `NAM/lstm.h`: `Eigen::placeholders::lastN` -> `Eigen::lastN` (Eigen 3.4.0 has no `placeholders` namespace) |
| nlohmann/json 3.12.0 | `third_party/NeuralAmpModelerCore/Dependencies/nlohmann/` | 3.12.0 | MIT, Niels Lohmann | none |
| Eigen 3.4.0 (+ `unsupported/Eigen/FFT`) | `third_party/eigen/` | 3.4.0 (Debian/Ubuntu `libeigen3-dev`) | MPL 2.0 (built with `EIGEN_MPL2_ONLY`) | none |
| libcurl headers (types and constants only; the library itself is the Force's own `libcurl.so.4`, loaded with `dlopen`) | `third_party/curl-include/` | Ubuntu `libcurl4-openssl-dev` | curl licence | none |

The idea, the API usage, the calibration / normalization rules and the IR handling follow TONE3000's open-source plugin
(MIT, commit c461f11cdfd5ffe4bb19baff11b9c48190ae8f30, https://github.com/tone-3000/tone3000-plugin). No code of it is copied;
it was read as the reference for the TONE3000 web API (device-code sign-in, search, download) and for the gain rules.
Licence text: `third_party/licenses/TONE3000-plugin-MIT.txt`.

`third_party/mpc-vst-plugins` is the framework checkout (sunskiefer/mpc-vst-plugins @ 3845bea) and is not part of this
repo: point `MPC_VST` at it, or add it as a submodule.
