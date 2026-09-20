"""Package the CK helper, authoring library, and corresponding helper source."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,zipfile

ROOT=Path(__file__).resolve().parents[2]

STANDALONE='''cmake_minimum_required(VERSION 3.25)
project(MirrorsOfFalloutCK LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "Use Windows and the Visual Studio C++ toolchain")
endif()
set(MIRROR_CK_STB_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/third_party" CACHE PATH "stb headers")
enable_testing()
add_subdirectory(tools/ck)
'''

def payload(repo_root=ROOT):
    root=repo_root.resolve()
    spec=importlib.util.spec_from_file_location('ck_author_assets',root/'tools/authoring/build_assets.py')
    author_assets=importlib.util.module_from_spec(spec);spec.loader.exec_module(author_assets)
    release=root/'build/ALL/tools/ck/Release'
    files={}
    for path,expected in author_assets.assets().items():
        data=(root/'package-mirrors'/path).read_bytes()
        if data!=expected:raise ValueError('Player authoring asset differs from its generator: '+path)
        files['Data/'+path]=data
    # The one ESM also defines the workshop objects. Include their meshes so the
    # CK kit can preview every object it loads, without a second content plugin.
    catalogue=json.loads((root/'tools/workshop/catalogue.json').read_text(encoding='utf-8'))
    for model in catalogue['models']:
        path='Meshes/'+model['model'].replace('\\','/')
        files['Data/'+path]=(root/'package-mirrors'/path).read_bytes()
    files['Data/README-MirrorAuthoring.txt']=(root/'docs/MIRROR_CREATION_KIT.md').read_bytes()
    files['MirrorAuthoring/README.txt']=(root/'tools/ck/README.md').read_bytes()
    files['MirrorAuthoring/COPYING']=(root/'COPYING').read_bytes()
    for name in ['MirrorsOfFalloutCK.exe','MirrorsOfFalloutCK.dll']:
        binary=(release/name).read_bytes()
        if not binary.startswith(b'MZ'):raise ValueError('Missing built helper: '+name)
        files['MirrorAuthoring/'+name]=binary
    source=[p for p in (root/'tools/ck').rglob('*') if p.is_file() and '__pycache__' not in p.parts]
    source += [root/path for path in [
        'src/Features/MirrorAuthoringGeometry.h','src/Features/MirrorDefinitionRegistry.h',
        'tools/authoring/build_assets.py','tools/workshop/build_plugin.py','tools/workshop/catalogue.json',
        'docs/MIRROR_CREATION_KIT.md','COPYING']]
    for path in source:files['Source/'+path.relative_to(root).as_posix()]=path.read_bytes()
    files['Source/CMakeLists.txt']=STANDALONE.encode()
    stb=(root/'build/ALL/vcpkg_installed/x64-windows-static-md/include/stb_image.h').read_bytes()
    license=stb[stb.rfind(b'ALTERNATIVE A - MIT License'):].split(b'------------------------------------------------------------------------------',1)[0]
    assert b'Copyright (c) 2017 Sean Barrett' in license and b'SOFTWARE.' in license
    files['MirrorAuthoring/THIRD-PARTY-NOTICES.txt']=b'stb_image zlib decompressor\nhttps://github.com/nothings/stb\n\n'+license
    files['Source/third_party/stb_image.h']=stb
    if any('CreationKit.enable' in p for p in files):raise ValueError('Legacy activation file must not be packaged')
    plugins=[p for p in files if p.startswith('Data/') and Path(p).suffix.lower() in ('.esp','.esm','.esl')]
    if plugins!=['Data/'+author_assets.PLUGIN]:raise ValueError('The kit must ship exactly the shared mirror ESM')
    return files


def build(output,archive,repo_root=ROOT):
    files=payload(repo_root)
    output.mkdir(parents=True,exist_ok=True)
    for name,data in files.items():
        path=output/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
    actual={p.relative_to(output).as_posix() for p in output.rglob('*') if p.is_file()}
    if actual!=set(files):raise ValueError('Unexpected files in package staging; use a fresh directory')
    archive.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
        for name,data in sorted(files.items()):
            info=zipfile.ZipInfo(name,(2026,9,8,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED;z.writestr(info,data)
    manifest=[dict(path=name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest()) for name,data in sorted(files.items())]
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None and set(z.namelist())==set(files)
        assert all(hashlib.sha256(z.read(i['path'])).hexdigest()==i['sha256'] for i in manifest)
    return dict(files=manifest,archive=str(archive),sha256=hashlib.sha256(archive.read_bytes()).hexdigest())

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--zip',type=Path,required=True)
    p.add_argument('--repo-root',type=Path,default=ROOT,help='Repository containing the player assets and matching ALL build')
    a=p.parse_args();print(json.dumps(build(a.output,a.zip,a.repo_root),indent=2))
