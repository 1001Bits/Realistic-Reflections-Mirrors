"""Fallout file, selection and native-validator integration tests."""
from pathlib import Path
import copy
import hashlib
import os
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools/mirror_creator'))
import nif_block_edit as nif
from mirror_creator_core import CreatorError, load_nif, select_surface
from mirror_creator_areas import combine_areas, toggle_area
from mirror_creator_export import export_nif, PANE_NAME, SCHEMA_NAME, SCHEMA_VALUE

FIXTURES = ROOT/'tools/ck/tests/fixtures'
if not FIXTURES.is_dir():
    FIXTURES = ROOT/'examples'


def fixture(name='Flat'):
    return (FIXTURES/(name+'.nif')).read_bytes()


def geometry(model, index, positions, triangles, full=False):
    av = nif.parse_avobject(model.blocks[index])
    off = av.tail_offset
    body = bytearray(model.blocks[index][off:off+46])
    # Keep the complete normal/tangent layout from the original half fixture.
    desc, _, _, _ = struct.unpack_from('<QIHI', body, 28)
    stride = (desc & 15)*4
    template = model.blocks[index][off+46:off+46+stride]
    if full:
        desc = (0x41B << 44) | 0x650407
        vertices = b''.join(struct.pack('<4f', *p, 0.)+template[8:] for p in positions)
    else:
        vertices = b''.join(struct.pack('<4e', *p, 0.)+template[8:] for p in positions)
    indices = b''.join(struct.pack('<3H', *t) for t in triangles)
    struct.pack_into('<QIHI', body, 28, desc, len(triangles), len(positions), len(vertices)+len(indices))
    model.blocks[index] = nif.serialize_avobject(av)+body+vertices+indices


def broken_fixture():
    model = nif.parse(fixture())
    positions = [(-55.,0.,-35.),(-15.,0.,-35.),(-15.,0.,35.),(-55.,0.,35.),
                 (15.,0.,-35.),(55.,0.,-35.),(55.,0.,35.),(15.,0.,35.)]
    geometry(model, 1, positions, [(0,1,2),(0,2,3),(4,5,6),(4,6,7)])
    return nif.serialize(model)


def separated_fixture(offset_y=0.):
    model = nif.parse(fixture())
    av = nif.parse_avobject(model.blocks[1])
    values = list(struct.unpack('<13f', av.transform)); values[0] = -45.
    av.transform = struct.pack('<13f', *values)
    model.blocks[1] = nif.serialize_avobject(av)+model.blocks[1][av.tail_offset:]
    other = copy.deepcopy(av); values[0] = 45.; values[1] = offset_y
    other.transform = struct.pack('<13f', *values)
    index = nif.append_block(model, 'BSTriShape', nif.serialize_avobject(other)+model.blocks[1][av.tail_offset:])
    parent = nif.parse_node(model.blocks[0]); parent.children.append(index)
    model.blocks[0] = nif.serialize_node(parent)
    return nif.serialize(model), index


def prepared(raw, areas=None):
    doc = load_nif(raw, 'author.nif')
    if areas is None:
        mesh = doc.meshes[0]
        areas = select_surface(doc, mesh.block, mode='part', allow_disconnected=True)
    return export_nif(doc, 'placeable', areas)


class CreatorTests(unittest.TestCase):
    def test_lossless_import_and_real_fo4_formats(self):
        for name in ('Flat','Hole','CurvedReject'):
            raw = fixture(name)
            self.assertEqual(nif.serialize(nif.parse(raw)), raw)
            doc = load_nif(raw)
            self.assertEqual(doc.data, raw)
        model = nif.parse(fixture()); original = load_nif(fixture()).meshes[0]
        geometry(model, 1, original.positions, original.triangles, full=True)
        self.assertEqual(load_nif(nif.serialize(model)).meshes[0].positions, original.positions)

    def test_output_marker_shader_and_no_source_changes(self):
        raw = fixture(); before = hashlib.sha256(raw).hexdigest()
        output, report = prepared(raw)
        model = nif.parse(output); root = nif.parse_node(model.blocks[0])
        markers = [model.blocks[i] for i in root.av.extra_data if model.type_name(i) == 'NiStringExtraData']
        self.assertEqual([(model.strings[a],model.strings[b]) for a,b in (struct.unpack('<II',v) for v in markers)],
                         [(SCHEMA_NAME,SCHEMA_VALUE)])
        pane = load_nif(output).mesh(report['paneBlock'])
        self.assertEqual(pane.name, PANE_NAME)
        self.assertEqual(hashlib.sha256(raw).hexdigest(), before)
        self.assertEqual(model.blocks[2:4], nif.parse(raw).blocks[2:4])
        av = nif.parse_avobject(model.blocks[report['paneBlock']])
        shader = struct.unpack_from('<i',model.blocks[report['paneBlock']],av.tail_offset+20)[0]
        self.assertEqual(len(model.blocks[shader]),140)
        self.assertIn('MOF_MirrorSurface.bgsm',model.strings[struct.unpack_from('<I',model.blocks[shader],4)[0]])

    def test_hole_preserves_contour_and_area(self):
        source = load_nif(fixture('Hole')); selected = select_surface(source,1,mode='part')
        output, report = export_nif(source,'placeable',selected)
        final = select_surface(load_nif(output),report['paneBlock'],mode='part')
        self.assertEqual(selected['triangleCount'],final['triangleCount'])
        self.assertAlmostEqual(selected['area'],final['area'])
        self.assertLess(final['area'],final['width']*final['height'])

    def test_subset_preserves_unselected_vertex_bytes_collision_and_shader(self):
        model = nif.parse(broken_fixture())
        # Collision is an unrelated opaque block pointing to the original AVObject.
        collision = nif.append_block(model,'bhkNPCollisionObject',struct.pack('<IHII',1,128,0xffffffff,0))
        av = nif.parse_avobject(model.blocks[1]); av.collision=collision
        model.blocks[1] = nif.serialize_avobject(av)+model.blocks[1][av.tail_offset:]
        raw = nif.serialize(model); doc = load_nif(raw)
        selection = select_surface(doc,1,seed=0)
        out, report = export_nif(doc,'placeable',selection); after = nif.parse(out)
        off = nif.parse_avobject(model.blocks[1]).tail_offset
        self.assertEqual(after.blocks[1][off+46:-12],model.blocks[1][off+46:-24])
        self.assertEqual(after.blocks[1][-12:],model.blocks[1][-12:])
        self.assertEqual(nif.parse_avobject(after.blocks[1]).collision,collision)
        self.assertEqual(after.blocks[collision],model.blocks[collision])
        self.assertEqual(after.blocks[2],model.blocks[2])
        self.assertEqual(len(load_nif(out).mesh(report['paneBlock']).triangles),2)

    def test_whole_selection_preserves_collision_owner(self):
        model = nif.parse(fixture()); av = nif.parse_avobject(model.blocks[1]); av.collision=0
        model.blocks[1] = nif.serialize_avobject(av)+model.blocks[1][av.tail_offset:]
        out, report = prepared(nif.serialize(model)); after = nif.parse(out)
        self.assertEqual(after.type_name(1),'NiNode')
        self.assertEqual(nif.parse_node(after.blocks[1]).av.collision,0)
        self.assertEqual(nif.parse_avobject(after.blocks[report['paneBlock']]).collision,-1)

    def test_shift_combines_parts_and_keeps_gap(self):
        raw, other = separated_fixture(); doc = load_nif(raw)
        first = select_surface(doc,1,seed=0); second = select_surface(doc,other,seed=0)
        selection = toggle_area(doc,first,second)
        out, report = export_nif(doc,'placeable',selection)
        self.assertEqual(report['selectedAreas'],2)
        pane = load_nif(out).mesh(report['paneBlock'])
        self.assertEqual(len(pane.triangles),4)
        self.assertTrue(all(p[0] < 0 for p in pane.face(0)))
        self.assertTrue(all(p[0] > 0 for p in pane.face(2)))
        remaining = toggle_area(doc,selection,first)
        self.assertEqual(len(remaining['areas']),1)

    def test_rotated_scaled_nodes_and_reverse_facing(self):
        model = nif.parse(fixture()); av = nif.parse_avobject(model.blocks[0])
        av.transform=struct.pack('<13f',23.,71.,-12.,0.,-1.,0.,1.,0.,0.,0.,0.,1.,2.)
        node=nif.parse_node(model.blocks[0]);node.av=av;model.blocks[0]=nif.serialize_node(node)
        doc=load_nif(nif.serialize(model));s=select_surface(doc,1,mode='part',flip=True)
        # Serialized columns describe a -90 degree Z rotation, then scale/translate.
        local=load_nif(fixture()).meshes[0].positions
        self.assertEqual(doc.mesh(1).positions,[(23.+2*y,71.-2*x,-12.+2*z) for x,y,z in local])
        out,report=export_nif(doc,'placeable',s)
        actual=select_surface(load_nif(out),report['paneBlock'],mode='part')
        self.assertEqual(actual['normal'],s['normal'])
        self.assertEqual(actual['origin'],s['origin'])

    def test_combine_parts_with_different_rotations(self):
        raw,other=separated_fixture();model=nif.parse(raw)
        av=nif.parse_avobject(model.blocks[other]);tail=model.blocks[other][av.tail_offset:]
        # +90 degree Y rotation in NIF's column-major order, translated right.
        av.transform=struct.pack('<13f',90.,0.,0.,0.,0.,-1.,0.,1.,0.,1.,0.,0.,1.)
        model.blocks[other]=nif.serialize_avobject(av)+tail
        doc=load_nif(nif.serialize(model));local=load_nif(fixture()).meshes[0].positions
        self.assertEqual(doc.mesh(other).positions,[(90.+z,y,-x) for x,y,z in local])
        selection=combine_areas(doc,[select_surface(doc,m.block,seed=0) for m in doc.meshes])
        output,report=export_nif(doc,'placeable',selection)
        pane=load_nif(output).mesh(report['paneBlock'])
        self.assertEqual([p for t in pane.triangles for p in (pane.positions[i] for i in t)],
                         [p for m in doc.meshes for t in m.triangles for p in (m.positions[i] for i in t)])

    def test_invalid_or_ambiguous_selections_rejected(self):
        doc=load_nif(fixture('CurvedReject'))
        with self.assertRaises(CreatorError):select_surface(doc,1,mode='part')
        raw,other=separated_fixture(2.);doc=load_nif(raw)
        with self.assertRaises(CreatorError):combine_areas(doc,[select_surface(doc,1,seed=0),select_surface(doc,other,seed=0)])
        output,report=prepared(broken_fixture(),select_surface(load_nif(broken_fixture()),1,seed=0))
        doc=load_nif(output)
        with self.assertRaises(CreatorError):export_nif(doc,'placeable',select_surface(doc,1,seed=0))
        reexport,_=export_nif(doc,'placeable',select_surface(doc,report['paneBlock'],mode='part'))
        self.assertEqual(sum(m.name==PANE_NAME for m in load_nif(reexport).meshes),1)

    def test_corrupt_skinned_animated_and_other_game_files_rejected(self):
        raw=fixture()
        for length in range(len(raw)):
            with self.assertRaises(CreatorError):load_nif(raw[:length])
        for offset,value,fmt in ((52,100,'<I'),(52,83,'<I'),(48,0xffffffff,'<I')):
            damaged=bytearray(raw);struct.pack_into(fmt,damaged,offset,value)
            with self.assertRaises(CreatorError):load_nif(bytes(damaged))
        for offset,value in ((8,2),(88,0)):
            model=nif.parse(raw);block=bytearray(model.blocks[1]);struct.pack_into('<i',block,offset,value);model.blocks[1]=block
            with self.assertRaises(CreatorError):load_nif(nif.serialize(model))
        model=nif.parse(raw);block=bytearray(model.blocks[1]);struct.pack_into('<H',block,len(block)-2,65535);model.blocks[1]=block
        with self.assertRaises(CreatorError):load_nif(nif.serialize(model))

    @unittest.skipUnless(os.environ.get('MIRROR_CK_VALIDATOR'),'Set MIRROR_CK_VALIDATOR for independent native NIF validation')
    def test_native_fo4_reader_accepts_exported_panes(self):
        with tempfile.TemporaryDirectory() as tmp:
            data=Path(tmp);(data/'Meshes').mkdir()
            for name,raw in [('Flat',fixture()),('Hole',fixture('Hole')),('Combined',separated_fixture()[0])]:
                doc=load_nif(raw)
                selected=combine_areas(doc,[select_surface(doc,m.block,mode='part') for m in doc.meshes])
                out,report=export_nif(doc,'placeable',selected)
                file=data/'Meshes'/f'{name}.nif';file.write_bytes(out)
                result=subprocess.run([os.environ['MIRROR_CK_VALIDATOR'],'--model',str(data),file.name],capture_output=True,text=True,check=True)
                self.assertIn(f"{report['paneBlock']} | {PANE_NAME}",result.stdout)
                self.assertIn('AVAILABLE',result.stdout)


if __name__=='__main__':unittest.main()
