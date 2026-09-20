# Fallout 4 Creators package: source and executable review

This folder contains the exact **Realistic Reflections - Mirrors 1.0 - Creators.zip**
release and all **129 files** extracted from it, including the source for both
authoring tools, their launchers, compiled CK binaries, Python runtime, assets,
tests and license notices. The additional review files in this folder are not
part of the distributed ZIP.

[Download the exact Creators ZIP](Realistic%20Reflections%20-%20Mirrors%201.0%20-%20Creators.zip).
Its SHA256 is:

```text
557990f4d8d1b911a066f3b696018af6eead96d3b8821278f29359ceed85dd1d
```

## Executables and corresponding source

| Distributed file | Purpose and source |
| --- | --- |
| `MirrorAuthoring/MirrorsOfFalloutCK.exe` | CK launcher. Full C++ source: [Launcher.cpp](Source/tools/ck/Launcher.cpp) and [Win32.h](Source/tools/ck/Win32.h). |
| `MirrorAuthoring/MirrorsOfFalloutCK.dll` | Alternate Textures picker and material-swap integration. Full source: [Source/tools/ck](Source/tools/ck), shared geometry headers in [Source/src/Features](Source/src/Features), and the included [stb header](Source/third_party/stb_image.h). |
| `Mirror Creator/runtime/python.exe`, `pythonw.exe`, `python314.dll` and accompanying runtime libraries | Official Python 3.14.7 embeddable x64 distribution. The 36 upstream files are unmodified; only `python314._pth` is replaced to set local import paths. Provenance is detailed below. |
| `Mirror Creator/Start Mirror Creator.cmd` and `tools/mirror_creator/Start-MirrorCreator.ps1` | Text launchers for the bundled Python application. [Launcher source](Mirror%20Creator/tools/mirror_creator/Start-MirrorCreator.ps1). |
| `Mirror Creator/tools/mirror_creator/*.py` and `mirror_creator_web/` | Complete NIF parser, selector, exporter, local server and browser UI. [Application source](Mirror%20Creator/tools/mirror_creator). These are readable source files, not a frozen application. |

The CK EXE and DLL are unsigned. The bundled `python.exe`, `pythonw.exe` and
`python314.dll` retain valid Python Software Foundation Authenticode signatures
as checked when preparing this review copy. File hashes are in
[SHA256SUMS.txt](SHA256SUMS.txt); the ZIP also includes two complete package manifests.

## What the tools do

**Mirror Creator** reads a NIF selected by the author, displays its geometry, and
downloads a new prepared NIF when Save is pressed. Its server binds only to
`127.0.0.1`, on port 8766 by default. The browser UI, Three.js viewer and Python
runtime are bundled. Model processing stays on the local computer. Host/origin
checks restrict access to the local UI. The Close button stops the server.
The launcher uses the bundled `pythonw.exe`; the source launcher can fall back to
a locally installed Python when the bundled runtime is absent.

**The CK helper** opens or attaches to the verified Fallout Creation Kit
1.11.240.0 executable. The launcher checks its whole-file SHA256, then uses
`SetWindowsHookExW(WH_CALLWNDPROCRET, ...)` on that editor's UI thread to load
the bundled helper DLL. This is how it adds the material picker. It reads CK
model data and uses verified CK functions to edit Material Swap records that the
author saves in their own plugin. The hook is removed when the launcher/editor
session ends. The source is included so this behavior can be inspected directly.

Usage instructions are in [README.txt](README.txt), the
[Mirror Creator guide](Mirror%20Creator/docs/MIRROR_CREATOR.md) and the
[CK helper guide](MirrorAuthoring/README.txt).

## Python and viewer provenance

The runtime comes from the official
[Python 3.14.7 embeddable x64 archive](https://www.python.org/ftp/python/3.14.7/python-3.14.7-embed-amd64.zip).
The upstream archive's SHA256 is:

```text
d297e5ff019966817ad8502465176139f2d3d840fa4ed84b13bed399a6ab1f15
```

The corresponding [CPython source archive](https://www.python.org/ftp/python/3.14.7/Python-3.14.7.tar.xz)
and [upstream Windows dependency inventory](https://www.python.org/ftp/python/3.14.7/python-3.14.7-embed-amd64.zip.spdx.json)
identify the runtime and its bundled libraries. All upstream runtime files were
compared byte-for-byte with that archive. The only local change is
[python314._pth](Mirror%20Creator/runtime/python314._pth):

```text
python314.zip
.
../tools/mirror_creator
```

Three.js 0.180.0 is included under MIT, with its license, original download URLs
and file hashes in [vendor](Mirror%20Creator/tools/mirror_creator/mirror_creator_web/vendor).
The custom tools use GNU GPL v3; Python retains its PSF license and stb its MIT
notice. All distributed license texts are retained in their original package locations.

## Verify and rebuild

Run this from the `Mirror Creator FO4` directory with Python, or use the bundled
interpreter. The verifier only reads and hashes files:

```powershell
& ".\Mirror Creator\runtime\python.exe" -B .\verify_package.py
```

The supplied CK source builds independently of the game-plugin source. With
Visual Studio 2022's Desktop development with C++ workload, CMake 3.25 or newer,
a Windows SDK and Python 3 installed:

```powershell
cmake -S Source -B ../MirrorCreatorFO4-review-build -G "Visual Studio 17 2022" -A x64
cmake --build ../MirrorCreatorFO4-review-build --config Release
ctest --test-dir ../MirrorCreatorFO4-review-build -C Release --output-on-failure
```

These commands build the CK EXE, DLL and native asset validator. They were tested
using only the files from this ZIP. Compiler versions, linker settings and paths
can change the hashes of a rebuilt binary; `SHA256SUMS.txt` identifies the exact
distributed binaries.

Run the NIF tool tests, including integration with that native reader:

```powershell
$env:MIRROR_CK_VALIDATOR = (Resolve-Path ../MirrorCreatorFO4-review-build/tools/ck/Release/mirror_ck_asset_tests.exe).Path
& ".\Mirror Creator\runtime\python.exe" -B -m unittest discover -s "Mirror Creator/tests/mirror_creator" -p test_creator.py -v
```

The archive comparison, standalone CK build, native asset tests and all 11
Creator file tests passed when preparing this review copy. In-game visual
confirmation of the new prepared-NIF path remains pending.
