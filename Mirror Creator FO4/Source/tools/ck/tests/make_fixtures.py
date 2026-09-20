"""Original tiny meshes and plugins for CK helper validation; never player assets."""
from pathlib import Path
import copy,struct,sys,zlib

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
sys.path.insert(0,str(ROOT/'tools/workshop'))
sys.path.insert(0,str(ROOT/'tools/authoring'))
from build_plugin import record,string,field,form
from build_assets import plugin,bgsm,SOURCE,MATERIAL

def split(raw):
    pos=56
    for _ in range(4):pos+=1+raw[pos]
    prefix=raw[:pos];n=struct.unpack_from('<I',raw,48)[0]
    types=[];count=struct.unpack_from('<H',raw,pos)[0];pos+=2
    for _ in range(count):
        length=struct.unpack_from('<I',raw,pos)[0];pos+=4;types.append(raw[pos:pos+length]);pos+=length
    indices=struct.unpack_from('<'+str(n)+'H',raw,pos);pos+=2*n
    sizes=struct.unpack_from('<'+str(n)+'I',raw,pos);pos+=4*n
    count,_=struct.unpack_from('<II',raw,pos);pos+=8;strings=[]
    for _ in range(count):
        length=struct.unpack_from('<I',raw,pos)[0];pos+=4;strings.append(raw[pos:pos+length]);pos+=length
    groups=struct.unpack_from('<I',raw,pos)[0];pos+=4+groups*4
    blocks=[]
    for index,size in zip(indices,sizes):blocks.append([types[index],bytearray(raw[pos:pos+size])]);pos+=size
    return bytearray(prefix),strings,blocks,raw[pos:]

def join(parts):
    prefix,strings,blocks,footer=parts;prefix=bytearray(prefix);struct.pack_into('<I',prefix,48,len(blocks))
    types=list(dict.fromkeys(t for t,b in blocks))
    out=prefix+struct.pack('<H',len(types))
    for t in types:out+=struct.pack('<I',len(t))+t
    out+=struct.pack('<'+str(len(blocks))+'H',*(types.index(t) for t,b in blocks))
    out+=struct.pack('<'+str(len(blocks))+'I',*(len(b) for t,b in blocks))
    out+=struct.pack('<II',len(strings),max(map(len,strings),default=0))
    for s in strings:out+=struct.pack('<I',len(s))+s
    out+=struct.pack('<I',0)+b''.join(b for t,b in blocks)+footer
    return bytes(out)

def archive(path,files,version=1,compressed=True):
    entries=bytearray();names=bytearray();payload=bytearray();offset=24+len(files)*36
    for name,data in files:
        name=name.encode();packed=zlib.compress(data) if compressed else data
        entries+=struct.pack('<I4sIIQIII',0,b'nif\0',0,0,offset+len(payload),len(packed) if compressed else 0,len(data),0xbaadf00d)
        payload+=packed;names+=struct.pack('<H',len(name))+name
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_bytes(b'BTDX'+struct.pack('<I4sIQ',version,b'GNRL',len(files),offset+len(payload))+entries+payload+names)

def build(output):
    output.mkdir(parents=True,exist_ok=True)
    base=split((HERE/'fixtures/Flat.nif').read_bytes())
    assert join(base)==(HERE/'fixtures/Flat.nif').read_bytes()
    for file in (HERE/'fixtures').glob('*.nif'):(output/file.name).write_bytes(file.read_bytes())
    def save(name,value):(output/(name+'.nif')).write_bytes(join(value))
    exported=copy.deepcopy(base)
    exported[1].append(('C:\\Projects\\Fallout4\\Build\\PC\\Data\\Materials\\'+SOURCE).encode())
    struct.pack_into('<I',exported[2][2][1],4,len(exported[1])-1);save('ExportedMaterial',exported)
    animated=copy.deepcopy(base);struct.pack_into('<i',animated[2][0][1],8,2);save('Animated',animated)
    skinned=copy.deepcopy(base);struct.pack_into('<i',skinned[2][1][1],88,0);save('Skinned',skinned)
    hidden=copy.deepcopy(base);struct.pack_into('<I',hidden[2][0][1],12,15);save('HiddenParent',hidden)
    cyclic=copy.deepcopy(base);struct.pack_into('<I',cyclic[2][0][1],76,0);save('Cycle',cyclic)
    bad_index=copy.deepcopy(base);struct.pack_into('<H',bad_index[2][1][1],198,60000);save('BadIndex',bad_index)
    shared=copy.deepcopy(base);shared[2].append(copy.deepcopy(shared[2][1]));struct.pack_into('<I',shared[2][0][1],72,2);shared[2][0][1]+=struct.pack('<I',4)
    # Shapes may share a BGSM path, but each needs its own shader property:
    # native CK stores render-pass ownership on the property object itself.
    shared[2].append(copy.deepcopy(shared[2][2]));struct.pack_into('<i',shared[2][4][1],92,5);save('Shared',shared)
    shared_export=copy.deepcopy(shared);shared_export[1].append(exported[1][-1])
    struct.pack_into('<I',shared_export[2][5][1],4,len(shared_export[1])-1);save('SharedExportedMaterial',shared_export)
    two=copy.deepcopy(shared);two[1].extend([b'HelperFrame',b'Materials\\MirrorsOfFallout\\HelperTest\\FrameOriginal.bgsm'])
    struct.pack_into('<I',two[2][4][1],0,len(two[1])-2);struct.pack_into('<f',two[2][4][1],16,150.)
    struct.pack_into('<i',two[2][4][1],92,5)
    struct.pack_into('<I',two[2][5][1],4,len(two[1])-1);save('TwoParts',two)
    for version in [1,7,8]:
        for compressed in [False,True]:archive(output/f'archive{version}-{int(compressed)}'/'test.ba2',[('meshes/test.nif',join(base))],version,compressed)
    archive(output/'conflict'/'a.ba2',[('meshes/test.nif',join(base))])
    archive(output/'conflict'/'b.ba2',[('meshes/test.nif',join(two))])
    archive(output/'unknown'/'test.ba2',[('meshes/test.nif',join(base))],99)
    data=output/'Data';meshes=data/'Meshes/MirrorsOfFallout/HelperTest';meshes.mkdir(parents=True,exist_ok=True)
    for name in ['Flat','TwoParts','Shared','CurvedReject']:(meshes/(name+'.nif')).write_bytes((output/(name+'.nif')).read_bytes())
    materials=data/'Materials/MirrorsOfFallout/HelperTest';materials.mkdir(parents=True,exist_ok=True)
    for name in ['FrameOriginal','FrameReplacement']:(materials/(name+'.bgsm')).write_bytes(bgsm())
    stats=[]
    for i,(name,mesh,swap) in enumerate([
        ('MOFCKHelperNew','Flat',0),('MOFCKHelperPreserve','TwoParts',0x02000810),
        ('MOFCKHelperSharedSwapUser','TwoParts',0x02000810),('MOFCKHelperRejectShared','Shared',0),
        ('MOFCKHelperRejectCurved','CurvedReject',0),('MOFCKHelperCancel','Flat',0)]):
        fields=[string('EDID',name),field('OBND',struct.pack('<6h',-32,-1,-48,190,1,48)),string('MODL',f'MirrorsOfFallout\\HelperTest\\{mesh}.nif')]
        if swap:fields.append(form('MODS',swap))
        fields.append(field('DNAM',struct.pack('<fIff',90.,0,1.,1.)))
        stats.append(record('STAT',0x02000800+i,fields))
    swaps=[record('MSWP',0x02000810,[string('EDID','MOFCKHelperFrameSwap'),
        string('BNAM',r'MirrorsOfFallout\HelperTest\FrameOriginal.bgsm'),string('SNAM',r'MirrorsOfFallout\HelperTest\FrameReplacement.bgsm'),field('CNAM',struct.pack('<f',.375)),
        string('BNAM',r'MirrorsOfFallout\HelperTest\UnusedOriginal.bgsm'),string('SNAM',r'MirrorsOfFallout\HelperTest\FrameReplacement.bgsm'),field('CNAM',struct.pack('<f',.625))])]
    (data/'MirrorsOfFallout-HelperTest.esp').write_bytes(plugin({'STAT':stats,'MSWP':swaps},['Fallout4.esm','Realistic Reflections - Mirrors.esm']))
    (output/'ready.txt').write_text('Original CK helper fixtures\n')

if __name__=='__main__':build(Path(sys.argv[1]))
