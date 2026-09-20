"""Create the shared Fallout mirror ESM and original material-tag assets.

FO4 uses MODS -> MSWP, not Skyrim's MODS alternate texture array. TXST.MNAM
also names the material for inspection, but the runtime tag is the applied BGSM.
Record schema: TES5Edit/dev-4.1.5/Core/wbDefinitionsFO4.pas.
BGSM v2 schema: ousnius/Material-Editor/MaterialLib/{BaseMaterialFile,BGSM}.cs.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import sys
import zipfile

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/workshop'))
from build_plugin import field, string, form, record, group, build_plugin, PLUGIN, MATERIAL, TEXTURES, SOURCE


def dds(color):
    header=[124,0x100f,4,4,16,0,0]+[0]*11+[32,0x41,0,32,0xff,0xff00,0xff0000,0xff000000,0x1000,0,0,0,0]
    return b'DDS '+struct.pack('<31I',*header)+bytes(color)*16


def bgsm():
    # Version 2 works on .163, .240 and VR. Opaque, depth-writing, no environment/SSR,
    # emissive, transparency, tessellation, skin, tree or palette shaders.
    raw=bytearray(b'BGSM'+struct.pack('<II5fBII',2,3,0.,0.,1.,1.,1.,0,0,0))
    raw+=bytes([0,0,1,1,0,0,0,1,0,0,0,0])  # alpha/depth through refraction-falloff
    raw+=struct.pack('<fBfB',0.,0,0.,0)
    assert len(raw)==63
    def text(value):
        return struct.pack('<I',len(value)+1)+value.encode('ascii')+b'\0'
    for name in ['mirror_surface.dds','mirror_surface_n.dds','mirror_surface_s.dds','','','','','','']:
        raw+=text(TEXTURES+name if name else '')
    raw+=struct.pack('<BBffBfB3fff7f',0,0,2.,0.,0,.3,0,1.,1.,1.,0.,0.,5.,-1.,-1.,-1.,-1.,-1.,-1.)
    raw+=text('')
    raw+=struct.pack('<BBf',0,0,1.) # aniso, emit, emittance multiplier
    raw+=bytes(12) # model-space normals through hair
    raw+=struct.pack('<3f',.5,.5,.5)
    raw+=bytes(4) # tree, facegen, skin-tint, tessellate
    raw+=struct.pack('<6fB',-.5,10.,1.,1.,0.,1.,0)
    return bytes(raw)


def plugin(groups,masters,master=False):
    count=sum(len(values) for values in groups.values())
    header=bytearray(record('TES4',0,[field('HEDR',struct.pack('<fII',1.,count+len(groups),0x800+count)),
        string('CNAM','Realistic Reflections - Mirrors'),string('SNAM','Realistic Reflections - Mirrors 1.0. Flat static mirror authoring material library.'),
        *(item for name in masters for item in [string('MAST',name),field('DATA',bytes(8))])]))
    struct.pack_into('<I',header,8,1 if master else 0)
    return bytes(header)+b''.join(group(kind,values) for kind,values in groups.items())


def assets():
    manifest=json.loads((ROOT/'tools/workshop/catalogue.json').read_text(encoding='utf-8'))
    return {PLUGIN:build_plugin(manifest),
        'Materials/'+MATERIAL.replace('\\','/'):bgsm(),
        'Textures/'+TEXTURES.replace('\\','/')+'mirror_surface.dds':dds((0,0,0,255)),
        'Textures/'+TEXTURES.replace('\\','/')+'mirror_surface_n.dds':dds((128,128,255,255)),
        'Textures/'+TEXTURES.replace('\\','/')+'mirror_surface_s.dds':dds((0,0,0,0))}


def build(output,archive=None):
    for name in ('MirrorsOfFallout-Authoring.esm', 'MirrorsOfFalloutWorkshop.esp',
                 'Realistic Reflections - Mirrors.esp'):
        if (output/name).exists():
            raise ValueError('Retire '+name+' before updating, or use a fresh output directory')
    files=assets()
    for relative,data in files.items():
        path=output/relative;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
    if archive:
        archive.parent.mkdir(parents=True,exist_ok=True)
        with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
            for name,data in sorted(files.items()):
                info=zipfile.ZipInfo(name,(2026,9,8,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED;z.writestr(info,data)
    return [{'path':name,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()} for name,data in files.items()]


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--zip',type=Path,
        help='Library update only; the main mod supplies the workshop meshes. Use tools/ck/package.py for the full CK kit.')
    args=p.parse_args();print(json.dumps(build(args.output,args.zip),indent=2))
