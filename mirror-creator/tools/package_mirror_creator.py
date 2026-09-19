"""Build a portable Windows Mirror Creator ZIP from an explicit source list.

Pass the verified official Python embeddable ZIP; no network/download or game
installation is modified by this packager. Output directory must be new.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import zipfile

ROOT=Path(__file__).resolve().parents[1]
PYTHON_SHA256='d297e5ff019966817ad8502465176139f2d3d840fa4ed84b13bed399a6ab1f15'
VERSION='0.5-preview'
FILES=[
    'Start Mirror Creator.cmd','LICENSE',
    'tools/Start-MirrorCreator.ps1','tools/mirror_creator.py','tools/mirror_creator_core.py',
    'tools/mirror_creator_export.py','tools/mirror_creator_areas.py','tools/nif_block_edit.py','tools/package_mirror_creator.py',
    'tests/test_mirror_creator.py','tests/test_mirror_creator_export.py','tests/test_mirror_creator_areas.py',
    'tests/test_mirror_creator_ck_parts.py','tests/mirror_creator_ck_parts_browser_smoke.py',
    'tests/mirror_creator_browser_smoke.py','tests/mirror_creator_areas_browser_smoke.py',
    'docs/MIRROR_CREATOR_PREVIEW.md','docs/MIRROR_NIF_AUTHORING.md',
    'docs/MIRROR_CREATOR_SHIFT_SELECTION.md',
    'dist/Data/meshes/mirrors_of_skyrim/mirror01.nif',
    'prototypes/hand-mirror/gilded-noble-round-filigree-v1/nifs/male-world.nif',
]
README='''Mirror Creator 0.5 preview — Realistic Reflections

1. Extract this entire ZIP to a folder you can write to.
2. Double-click Start Mirror Creator.cmd. A local browser preview opens.
3. Select NIF.
4. Click the pane from the side that should reflect.
   Shift-click to add or remove other areas on that same flat plane.
   The selection shows its CK part name and 3D Index for Alternate Textures.
5. Save downloads a prepared NIF with the same filename as the file you opened.

Drag to rotate, right-drag to pan, scroll to zoom. The checker outline shows
your selected surface. To change the reflective side, rotate the model and
click the other side. Open a saved NIF using Select NIF to edit it again.

The original NIF is never overwritten. All processing stays on your computer.
Python and the 3D viewer are included. No Skyrim installation, Python install,
Node, extra packages or internet connection is needed to use the tool.
Requires Windows 10/11 x64 and a WebGL 2 browser such as Edge or Chrome.
Share this complete ZIP; the localhost browser address only works on your PC.

IN-GAME STATUS
The exported NIF has its reflective surface, plane, front and exact triangle
outline embedded as extra data. It has an opaque black fallback pane.
World objects using that NIF reflect when Realistic Reflections is installed.
Do not also assign MOS_MirrorSurface on the same object. No enable file is
required. This ZIP is the authoring tool, not the game mod.

Use the NIF as an ordinary object model in your own Skyrim mod. No per-design
registration in our plugin is needed for the marked world surface. This tool
does not create ESP/item records, placement scripts, collision or equipment
fits. See docs/MIRROR_NIF_AUTHORING.md for exact status.

SUPPORTED INPUT
Static, unskinned Skyrim SE/VR stream-100 BSTriShape NIFs. Pick one flat region
or Shift-click several coplanar areas for one reflection. Gaps, round contours
and holes are preserved. LE, skinned/animated and
curved reflective surfaces are not supported. Materials/textures/collision
are preserved where unrelated to the selected pane but are not previewed.
Fallout 4 NIF import/export and its runtime recognition are not implemented.

If another preview is using port 8765, run in PowerShell:
  powershell -NoProfile -ExecutionPolicy Bypass -File .\\tools\\Start-MirrorCreator.ps1 -Port 8766

LICENSES AND SOURCE
Tool source is included under GPL-3.0 (LICENSE) when that file is present.
Three.js is MIT with license and provenance in tools/mirror_creator_web/vendor.
The included Python 3.14.7 embeddable runtime carries its PSF license in
runtime/LICENSE.txt. The examples are first-party Realistic Reflections assets.
'''


def web_files():
    return [str(p.relative_to(ROOT)).replace('\\','/') for p in sorted((ROOT/'tools/mirror_creator_web').rglob('*')) if p.is_file()]


def source_payload():
    """Tool files without the bundled Python runtime."""
    files={}
    for relative in FILES+web_files():
        source=ROOT/relative
        if relative=='LICENSE' and not source.is_file():
            continue
        if source.is_symlink():
            raise ValueError('Package source must not be a symlink: '+relative)
        files[relative.replace('\\','/')]=source.read_bytes()
    files['README.txt']=README.encode('utf-8')
    return files


def add_python_runtime(files, python_zip):
    raw=python_zip.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=PYTHON_SHA256:
        raise ValueError('Python embeddable ZIP does not match the verified 3.14.7 x64 download.')
    with zipfile.ZipFile(python_zip) as archive:
        for info in archive.infolist():
            relative=PurePosixPath(info.filename)
            if relative.is_absolute() or '..' in relative.parts or '\\' in info.filename or ':' in info.filename:
                raise ValueError('Unsafe runtime archive member.')
            if info.is_dir():
                continue
            files['runtime/'+str(relative)]=archive.read(info)
    files['runtime/python314._pth']=b'python314.zip\n.\n../tools\n'
    return files


def write_stage(files, stage):
    for relative, data in files.items():
        destination=stage/relative
        destination.parent.mkdir(parents=True,exist_ok=True)
        destination.write_bytes(data)


def package(python_zip, output):
    output.mkdir(parents=True,exist_ok=False)
    stage=output/f'MirrorCreator-{VERSION}-win64';stage.mkdir()
    files=add_python_runtime(source_payload(), python_zip)
    write_stage(files, stage)
    manifest=dict(name='Mirror Creator',version=VERSION,platform='Windows x64',
                  preparedNifExport=True,playableAddonExport=False,equippedHandRuntime=False,
                  ckPartIdentification=True,
                  python=dict(version='3.14.7',sha256=PYTHON_SHA256,url='https://www.python.org/ftp/python/3.14.7/python-3.14.7-embed-amd64.zip'),files=[])
    for path in sorted(stage.rglob('*')):
        if path.is_file():
            data=path.read_bytes();manifest['files'].append(dict(path=path.relative_to(stage).as_posix(),bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
    (stage/'package.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    destination=output/f'{stage.name}.zip'
    with zipfile.ZipFile(destination,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        for path in sorted(stage.rglob('*')):
            if path.is_file():archive.write(path,path.relative_to(output).as_posix())
    result=dict(zip=str(destination),bytes=destination.stat().st_size,sha256=hashlib.sha256(destination.read_bytes()).hexdigest(),files=len(manifest['files'])+1)
    (output/'package-result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--python-zip',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();print(json.dumps(package(args.python_zip,args.output),indent=2))
