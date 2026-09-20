"""Package Fallout's portable Mirror Creator together with the compiled CK helper.

Takes a verified official Python embeddable archive and the matching ALL build.
No downloads, game changes or launches are performed. Use a new output directory.
"""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import importlib.util
import json
import zipfile

ROOT = Path(__file__).resolve().parents[2]
PYTHON_SHA256 = 'd297e5ff019966817ad8502465176139f2d3d840fa4ed84b13bed399a6ab1f15'
VERSION = '1.0'
SOURCE_FILES = [
    'mirror_creator.py', 'mirror_creator_core.py', 'mirror_creator_export.py',
    'mirror_creator_areas.py', 'nif_block_edit.py', 'Start-MirrorCreator.ps1',
    'Start Mirror Creator.cmd', 'README.txt', 'package.py',
    'mirror_creator_web/index.html', 'mirror_creator_web/app.js', 'mirror_creator_web/style.css',
    'mirror_creator_web/vendor/three.core.js', 'mirror_creator_web/vendor/three.module.js',
    'mirror_creator_web/vendor/OrbitControls.js', 'mirror_creator_web/vendor/LICENSE.three.txt',
    'mirror_creator_web/vendor/provenance.json',
]
README = '''Realistic Reflections - Mirrors 1.0 - Fallout 4 Creators

This download contains two authoring tools. Players install the main mod.
Use the matching player DLL package from this release, or a newer build with
prepared-NIF support; earlier DLLs do not recognise Mirror Creator exports.

MIRROR CREATOR
Extract the complete ZIP anywhere you can write, then open Mirror Creator and
double-click Start Mirror Creator.cmd. Select a Fallout NIF, click its pane,
Shift-click additional flat areas if needed, and Save. Use the prepared NIF as
a Static object's model in your own mod. Python and the browser viewer are
included. See Mirror Creator/README.txt and its docs/MIRROR_CREATOR.md.

CREATION KIT HELPER
To assign an existing whole model part in CK instead, copy Data and MirrorAuthoring
beside CreationKit.exe, then start MirrorAuthoring/MirrorsOfFalloutCK.exe.
Load Realistic Reflections - Mirrors.esm with your own plugin active. In a Static
object's Model Data > Alternate Textures, select a flat pane and assign
MOF_MirrorSurface. Place the object and save. This helper requires CK 1.11.240.0.
Do not additionally assign that material to a NIF saved by Mirror Creator.
The CK version restriction applies to the helper, not the standalone NIF tool.

Both workflows require the main mod and its enabled ESM in game, plus your
content files. The Data assets in this kit match the player package. Only one
flat reflection per Static object is supported. Test your result in game before
publishing. See Data/README-MirrorAuthoring.txt for geometry limitations.

The Creator source and licenses are in Mirror Creator. The compiled CK helper's
corresponding source is in Source, with its build instructions in
MirrorAuthoring/README.txt. This ZIP does not contain the F4SE player plugin.
'''


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def manifest(files):
    return [dict(path=name, bytes=len(data), sha256=sha256(data)) for name, data in sorted(files.items())]


def creator_payload(root, python_zip):
    files = {}
    for name in SOURCE_FILES:
        source = root/'tools/mirror_creator'/name
        if source.is_symlink():
            raise ValueError('Source files must not be symlinks: '+name)
        files['tools/mirror_creator/'+name] = source.read_bytes()
    files['Start Mirror Creator.cmd'] = (b'@echo off\r\n'
        b'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\\mirror_creator\\Start-MirrorCreator.ps1"\r\n'
        b'if errorlevel 1 pause\r\n')
    files['README.txt'] = files['tools/mirror_creator/README.txt']
    files['COPYING'] = (root/'COPYING').read_bytes()
    files['docs/MIRROR_CREATOR.md'] = (root/'docs/MIRROR_CREATOR.md').read_bytes()
    for name in ('Flat', 'Hole', 'CurvedReject'):
        path = 'tools/ck/tests/fixtures/'+name+'.nif'
        files[path] = (root/path).read_bytes()
        if name != 'CurvedReject':
            files['examples/'+name+'.nif'] = files[path]
    for name in ('test_creator.py', 'browser_smoke.py'):
        path = 'tests/mirror_creator/'+name
        files[path] = (root/path).read_bytes()
    if sha256(python_zip.read_bytes()) != PYTHON_SHA256:
        raise ValueError('Expected the verified official Python 3.14.7 embeddable x64 ZIP.')
    with zipfile.ZipFile(python_zip) as archive:
        for info in archive.infolist():
            path = PurePosixPath(info.filename)
            if path.is_absolute() or '..' in path.parts or '\\' in info.filename or ':' in info.filename:
                raise ValueError('Unsafe runtime archive entry.')
            if not info.is_dir():
                files['runtime/'+str(path)] = archive.read(info)
    files['runtime/python314._pth'] = b'python314.zip\n.\n../tools/mirror_creator\n'
    # Keep offline vendor provenance verifiable after packaging.
    vendor = 'tools/mirror_creator/mirror_creator_web/vendor/'
    for entry in json.loads(files[vendor+'provenance.json'])['files']:
        if sha256(files[vendor+entry['file']]) != entry['sha256']:
            raise ValueError('Viewer dependency differs from its recorded provenance.')
    receipt = dict(name='Mirror Creator for Fallout 4', version=VERSION, game='fallout4',
                   preparedNifSchema=1, multiAreaSelection=True, platform='Windows x64',
                   python=dict(version='3.14.7', sha256=PYTHON_SHA256,
                               url='https://www.python.org/ftp/python/3.14.7/python-3.14.7-embed-amd64.zip'),
                   files=manifest(files))
    files['package.json'] = (json.dumps(receipt, indent=2)+'\n').encode()
    return files


def build(output, archive, python_zip, repo_root=ROOT):
    root = repo_root.resolve()
    spec = importlib.util.spec_from_file_location('fallout_ck_package', root/'tools/ck/package.py')
    ck = importlib.util.module_from_spec(spec); spec.loader.exec_module(ck)
    files = ck.payload(root)
    files.update({'Mirror Creator/'+name: data for name, data in creator_payload(root, python_zip).items()})
    files['README.txt'] = README.encode()
    files['Source/docs/MIRROR_CREATOR.md'] = (root/'docs/MIRROR_CREATOR.md').read_bytes()
    dll = (root/'build/ALL/Release/RealisticReflectionsMirrors.dll').read_bytes()
    if not dll.startswith(b'MZ') or b'MOFMirrorSurface' not in dll:
        raise ValueError('Build the matching player DLL with prepared-NIF recognition first.')
    receipt = dict(name='Realistic Reflections - Mirrors - Fallout 4 Creators', version=VERSION,
                   preparedNifSchema=1, matchingPlayerDllSHA256=sha256(dll), files=manifest(files))
    files['package.json'] = (json.dumps(receipt, indent=2)+'\n').encode()
    output.mkdir(parents=True, exist_ok=False)
    for name, data in files.items():
        destination = output/name
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
    archive.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as bundle:
        for name, data in sorted(files.items()):
            bundle.writestr(name, data)
    with zipfile.ZipFile(archive) as bundle:
        if bundle.testzip() is not None or set(bundle.namelist()) != set(files):
            raise ValueError('Creators ZIP verification failed.')
        if any(bundle.read(name) != data for name, data in files.items()):
            raise ValueError('Creators ZIP file contents differ from staged files.')
    return dict(archive=str(archive), sha256=sha256(archive.read_bytes()), files=len(files),
                matchingPlayerDllSHA256=sha256(dll))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--zip', type=Path, required=True)
    parser.add_argument('--python-zip', type=Path, required=True)
    parser.add_argument('--repo-root', type=Path, default=ROOT)
    args = parser.parse_args()
    print(json.dumps(build(args.output, args.zip, args.python_zip, args.repo_root), indent=2))
