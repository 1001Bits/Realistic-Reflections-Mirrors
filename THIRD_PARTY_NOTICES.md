# Third-Party Notices

This notice covers the dependency set included with, or required to build, the
current source tree.
The files under `LICENSES/` retain the applicable license and copyright texts
identified below; the DirectX Shader Compiler notice is scoped to the three
retained components rather than the complete compiler distribution. They must accompany every binary
package and corresponding-source archive.

This file records third-party notices only. It does not grant a license for the
project's original source.

## Runtime and build dependencies

| Component | Audited version or revision | License | Verbatim notice copy | Authoritative local source used for the copy |
|---|---|---|---|---|
| CommonLibSSE-NG | 4.18.0, commit `8c4025b01fac2bea1bbe73a3a9da7b4fde338343` | MIT | `LICENSES/CommonLibSSE-NG-MIT.txt` | `extern/CommonLibSSE-NG/LICENSE` |
| OpenVR SDK header | 2.15.6 | BSD-3-Clause | `LICENSES/OpenVR-BSD-3-Clause.txt` | vendored by CommonLibSSE-NG |
| Microsoft Detours | 2025-06-20 | MIT | `LICENSES/Microsoft-Detours-MIT.txt` | vcpkg package notice |
| DirectXMath | 2026-03-12 | MIT | `LICENSES/DirectXMath-MIT.txt` | vcpkg package notice |
| DirectX Tool Kit | 2026-03-31 | MIT | `LICENSES/DirectXTK-MIT.txt` | vcpkg package notice |
| fmt | 12.1.0 | MIT | `LICENSES/fmt-MIT.txt` | vcpkg package notice |
| JSON for Modern C++ | 3.12.0 | MIT | `LICENSES/nlohmann-json-MIT.txt` | vcpkg package notice |
| rapidcsv | 8.92 | BSD-3-Clause | `LICENSES/rapidcsv-BSD-3-Clause.txt` | vcpkg package notice |
| spdlog | 1.17.0 | MIT | `LICENSES/spdlog-MIT.txt` | vcpkg package notice |
| Xbyak | 7.28 | BSD-3-Clause | `LICENSES/xbyak-BSD-3-Clause.txt` | vcpkg package notice |
| Microsoft DirectX Shader Compiler: token definitions, operand counts, container checksum | Retained files verified against commit `397ffb2264b4650c97b2418ec9a0f07228719317` | University of Illinois/NCSA | `LICENSES/DirectXShaderCompiler-Illinois.txt` | `extern/dxbc/`; upstream file headers and applicable first section of `LICENSE.TXT` |

The dependency list is intentionally conservative: it includes every direct
manifest dependency audited for this tree, even where a dependency is used only
by build tooling or arrives transitively in the final link. Microsoft Windows,
Direct3D, Skyrim, SKSE, and other external platform or game components are not
redistributed by this project and are not relicensed here.

