# Mirror Creator source

This folder contains the Mirror Creator 0.5 application source distributed in
`RealisticReflections-Creators-20260919.zip`. Its Python, JavaScript, HTML, CSS,
launchers, tests, documentation and first-party sample meshes are copied
byte-for-byte from that package. `source-manifest.json` records their SHA-256
hashes and the package identity for Nexus review. This README, manifest,
`.gitignore`, `.gitattributes` and restored GPL license are publication metadata.

## Run from source

Install Python 3.14.7, open a terminal in this `mirror-creator` folder, and run:

```powershell
python -B tools/mirror_creator.py --open
```

The application serves its bundled viewer on `127.0.0.1:8765`. Select a Skyrim
NIF, select the flat areas to reflect, and save the prepared NIF. No pip packages,
Node installation or game launch are needed. `Start Mirror Creator.cmd` also
works with a Python installation on PATH.

## Executables in the Nexus package

The package runs the included Python source with the Python Software Foundation's
CPython 3.14.7 Windows embeddable runtime. It does not compile the Creator into a
separate executable. Both executable files and every bundled DLL/PYD are
byte-identical to the official embeddable download; only `python314._pth` is
configured to add `../tools`. Runtime binaries are available in the Creators
package and the official download, and are not duplicated in this source folder.

- [Exact CPython source, v3.14.7](https://github.com/python/cpython/tree/v3.14.7)
- [Official Windows x64 embeddable runtime](https://www.python.org/ftp/python/3.14.7/python-3.14.7-embed-amd64.zip)
- [CPython Windows build instructions](https://github.com/python/cpython/blob/v3.14.7/PCbuild/readme.txt)

| File | SHA-256 |
|---|---|
| Creators ZIP | `2C2788D0533125BE53F1312F023713006304BFE707860D74D49C466CD36D2F1D` |
| Official Python ZIP | `D297E5FF019966817AD8502465176139F2D3D840FA4ED84B13BED399A6AB1F15` |
| `runtime/python.exe` | `4942B86A6597E5AEE0128DAA00050ED79BC21F6E709A78EB19CBFEB0C2F39AC9` |
| `runtime/pythonw.exe` | `C197268F7E7CF2848B8C1AE59BBD0E0C14DEFE668A2D365302137EA929B47769` |

To reproduce a portable Creator package, supply the exact official Python ZIP:

```powershell
python tools/package_mirror_creator.py --python-zip C:/Downloads/python-3.14.7-embed-amd64.zip --output C:/Temp/MirrorCreator-portable
```

The output directory must not already exist. The packager verifies the Python
archive hash before including it. The restored GPL license is additionally
included when packaging from this published source tree.

## Verification and licenses

```powershell
python -B -m unittest discover -s tests -p "test_mirror_creator*.py"
```

First-party Creator source is GPL-3.0; see [LICENSE](LICENSE). The bundled
Three.js viewer is MIT; see its [license](tools/mirror_creator_web/vendor/LICENSE.three.txt)
and [provenance](tools/mirror_creator_web/vendor/provenance.json). Python's PSF
license is retained in [runtime/LICENSE.txt](runtime/LICENSE.txt).
