"""Geometry, project integrity and local HTTP checks for the author preview."""
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import threading
import unittest
from unittest.mock import patch
from urllib.error import HTTPError
from urllib.request import Request, urlopen
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import mirror_creator_core as core
import nif_block_edit as nif
from mirror_creator import CreatorServer


def fixture(vertices=None, triangles=None, translation=(0,0,0), rotation=None, scale=1):
    vertices = vertices or [(0,0,0),(2,0,0),(2,3,0),(0,3,0)]
    triangles = triangles or [(0,1,2),(0,2,3)]
    rotation = rotation or (1,0,0,0,1,0,0,0,1)
    def av(name, transform):
        return struct.pack('<IiiI13fi',name,0,-1,0,*transform,-1)
    transform=(*translation,*rotation,scale)
    root=av(0,transform)+struct.pack('<IiI',1,1,0)
    geometry=b''.join(struct.pack('<4f',*v,0) for v in vertices)+b''.join(struct.pack('<3H',*t) for t in triangles)
    shape=av(1,(0,0,0,1,0,0,0,1,0,0,0,1,1))+struct.pack('<4fiiiQHHI',0,0,0,20,-1,-1,-1,(1<<44)|4,len(triangles),len(vertices),len(geometry))+geometry+struct.pack('<I',0)
    model=nif.Nif(b'Gamebryo File Format, Version 20.2.0.7\n',0x14020007,1,12,100,[b'\0']*3,['NiNode','BSTriShape'],[0,1],[len(root),len(shape)],['Root','Pane'],4,[],[root,shape],struct.pack('<Ii',1,0))
    return nif.serialize(model)


def rewrite_project(raw, edit=None, entries=None):
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        values={name:archive.read(name) for name in archive.namelist()}
    if edit:
        project=json.loads(values['project.json']); edit(project)
        values['project.json']=json.dumps(project).encode()
    if entries: values.update(entries)
    out=io.BytesIO()
    with zipfile.ZipFile(out,'w') as archive:
        for name,data in values.items(): archive.writestr(name,data)
    return out.getvalue()


class GeometryTests(unittest.TestCase):
    def test_nested_transform_plane_and_area(self):
        doc=core.load_nif(fixture(translation=(10,20,30),rotation=(0,0,1,0,1,0,-1,0,0),scale=2))
        self.assertEqual(doc.meshes[0].positions[2],(10,26,26))
        surface=core.select_surface(doc,1,seed=1)
        self.assertEqual(surface['faces'],[0,1]); self.assertEqual(surface['normal'],(1,0,0))
        self.assertAlmostEqual(surface['area'],24)
        self.assertEqual(len(surface['boundary']),4)
        self.assertEqual(sorted([surface['width'],surface['height']]),[4,6])

    def test_uv_seam_connectivity(self):
        doc=core.load_nif(fixture([(0,0,0),(2,0,0),(2,3,0),(0,0,0),(2,3,0),(0,3,0)],[(0,1,2),(3,4,5)]))
        self.assertEqual(core.select_surface(doc,1,seed=1)['faces'],[0,1])

    def test_combined_frame_and_pane_only_selects_flat_region(self):
        doc=core.load_nif(fixture([(0,0,0),(2,0,0),(2,3,0),(0,3,0),(2,3,2)],[(0,1,2),(0,2,3),(1,4,2)]))
        self.assertEqual(core.select_surface(doc,1,seed=0)['faces'],[0,1])
        with self.assertRaisesRegex(core.CreatorError,'not one flat'): core.select_surface(doc,1,mode='part')

    def test_front_back_are_not_combined(self):
        doc=core.load_nif(fixture(triangles=[(0,1,2),(0,2,3),(2,1,0),(3,2,0)]))
        self.assertEqual(core.select_surface(doc,1,seed=0)['faces'],[0,1])
        self.assertEqual(core.select_surface(doc,1,seed=2)['faces'],[2,3])

    def test_disconnected_flat_parts_rejected(self):
        doc=core.load_nif(fixture([(0,0,0),(1,0,0),(0,1,0),(5,0,0),(6,0,0),(5,1,0)],[(0,1,2),(3,4,5)]))
        self.assertEqual(core.select_surface(doc,1,seed=0)['faces'],[0])
        with self.assertRaisesRegex(core.CreatorError,'disconnected'): core.select_surface(doc,1,mode='part')

    def test_flipping_preserves_contour_and_faces(self):
        doc=core.load_nif(fixture()); a=core.select_surface(doc,1,seed=0); b=core.select_surface(doc,1,seed=0,flip=True)
        self.assertEqual(a['faces'],b['faces']); self.assertEqual(a['boundary'],b['boundary'])
        self.assertEqual(a['normal'],tuple(-v for v in b['normal']))
        self.assertEqual(a['origin'],b['origin'])

    def test_collapsed_triangle_rejected(self):
        doc=core.load_nif(fixture(triangles=[(0,0,1)]))
        with self.assertRaisesRegex(core.CreatorError,'collapsed'): core.select_surface(doc,1,seed=0)

    def test_nonmanifold_surface_rejected(self):
        doc=core.load_nif(fixture(triangles=[(0,1,2),(0,2,3),(0,1,2)]))
        with self.assertRaisesRegex(core.CreatorError,'non-manifold'): core.select_surface(doc,1,mode='part')

    def test_hole_in_surface_preserves_inner_contour(self):
        vertices=[(0,0,0),(4,0,0),(4,4,0),(0,4,0),(1,1,0),(3,1,0),(3,3,0),(1,3,0)]
        triangles=[(0,1,5),(0,5,4),(1,2,6),(1,6,5),(2,3,7),(2,7,6),(3,0,4),(3,4,7)]
        s=core.select_surface(core.load_nif(fixture(vertices,triangles)),1,seed=0)
        self.assertEqual(len(s['boundary']),8); self.assertAlmostEqual(s['area'],12)

    def test_real_standing_and_round_hand_contours(self):
        cases=[(ROOT/'dist/Data/meshes/mirrors_of_skyrim/mirror01.nif','TrueMirror:0',2,4),
               (ROOT/'prototypes/hand-mirror/gilded-noble-round-filigree-v1/nifs/male-world.nif','RRMirrorFallback:0',24,24)]
        for path,name,faces,edges in cases:
            with self.subTest(path=path):
                raw=path.read_bytes(); doc=core.load_nif(raw,path.name)
                pane=next(m for m in doc.meshes if m.name==name)
                surface=core.select_surface(doc,pane.block,seed=0)
                self.assertEqual(len(surface['faces']),faces); self.assertEqual(len(surface['boundary']),edges)
                self.assertEqual(doc.data,raw)


class ImportTests(unittest.TestCase):
    def test_truncated_file_rejected(self):
        for size in (0,20,45,80,len(fixture())-1):
            with self.subTest(size=size),self.assertRaises(core.CreatorError): core.load_nif(fixture()[:size])

    def test_unsupported_stream_and_endian_rejected(self):
        for relative,fmt,value in ((4,'<B',0),(13,'<I',83),(9,'<I',5000)):
            raw=bytearray(fixture()); struct.pack_into(fmt,raw,raw.index(b'\n')+1+relative,value)
            with self.subTest(relative=relative),self.assertRaises(core.CreatorError): core.load_nif(bytes(raw))

    def test_invalid_triangle_index_rejected(self):
        with self.assertRaisesRegex(core.CreatorError,'missing vertex'): core.load_nif(fixture(triangles=[(0,1,99)]))

    def test_nonfinite_positions_and_transforms_rejected(self):
        for raw in (fixture(vertices=[(0,0,0),(float('nan'),0,0),(0,1,0)],triangles=[(0,1,2)]),fixture(translation=(0,float('inf'),0))):
            with self.assertRaisesRegex(core.CreatorError,'non-finite|invalid'): core.load_nif(raw)

    def test_cyclic_scene_rejected(self):
        model=nif.parse(fixture()); block=bytearray(model.blocks[0]); struct.pack_into('<i',block,nif.parse_avobject(block).tail_offset+4,0);model.blocks[0]=bytes(block)
        with self.assertRaisesRegex(core.CreatorError,'cyclic'):core.load_nif(nif.serialize(model))

    def test_skinned_mesh_rejected(self):
        model=nif.parse(fixture());block=bytearray(model.blocks[1]);struct.pack_into('<i',block,nif.parse_avobject(block).tail_offset+16,0);model.blocks[1]=bytes(block)
        with self.assertRaisesRegex(core.CreatorError,'skinned'):core.load_nif(nif.serialize(model))

    def test_unknown_scene_geometry_rejected(self):
        model=nif.parse(fixture());model.types[1]='BSDynamicTriShape'
        with self.assertRaisesRegex(core.CreatorError,'BSDynamicTriShape'):core.load_nif(nif.serialize(model))

    def test_unknown_unreferenced_blocks_preserved(self):
        model=nif.parse(fixture());nif.append_block(model,'UnrecognizedMetadata',b'\x00opaque\xffdata');raw=nif.serialize(model)
        doc=core.load_nif(raw);self.assertEqual(doc.data,raw)

    def test_filename_has_no_path_or_control_characters(self):
        self.assertEqual(core.load_nif(fixture(),'C:\\private\\folder\\my mirror.nif').name,'my mirror.nif')
        self.assertEqual(core.load_nif(fixture(),'bad\r\n.nif').name,'bad.nif')

    def test_preview_has_no_hand_button_and_keeps_the_original_filename(self):
        html=(ROOT/'tools/mirror_creator_web/index.html').read_text(encoding='utf-8')
        js=(ROOT/'tools/mirror_creator_web/app.js').read_text(encoding='utf-8')
        self.assertNotIn('Hand mirror', html)
        self.assertNotIn('Wall mirror', html)
        self.assertNotIn('mirror-type', html)
        self.assertNotIn('-reflective', js)
        self.assertIn("mirrorType:'placeable'", js)
        self.assertIn('state.document.name', js)
        self.assertIn("endsWith('.nif') ? name : 'mirror.nif'", js)


class ProjectTests(unittest.TestCase):
    def setUp(self):
        self.doc=core.load_nif(fixture(),'own.nif');self.surface=core.select_surface(self.doc,1,seed=0,flip=True)
        self.project=core.save_project(self.doc,'hand',self.surface)

    def test_both_mirror_types_restore_exact_model_selection_and_facing(self):
        for kind in ('hand','placeable'):
            raw=core.save_project(self.doc,kind,self.surface);doc,restored,surface=core.load_project(raw)
            self.assertEqual(doc.data,self.doc.data);self.assertEqual(restored,kind);self.assertEqual(surface,self.surface)
            self.assertEqual(doc.name,'own.nif');self.assertEqual(core.save_project(doc,kind,surface),raw)

    def test_changed_original_nif_rejected(self):
        raw=rewrite_project(self.project,entries={'asset.nif':fixture(scale=2)})
        with self.assertRaisesRegex(core.CreatorError,'changed since'):core.load_project(raw)

    def test_missing_or_duplicated_faces_rejected(self):
        for faces in ([-1],[999],[0,0],[],[True],None):
            with self.subTest(faces=faces),self.assertRaises(core.CreatorError):
                core.load_project(rewrite_project(self.project,lambda p:p['selection'].update(faces=faces)))

    def test_unknown_version_and_profile_rejected(self):
        for edit in (lambda p:p.update(version=2),lambda p:p.update(mirrorType='world-static'),lambda p:p.update(playableAddon=True)):
            with self.assertRaises(core.CreatorError):core.load_project(rewrite_project(self.project,edit))

    def test_unexpected_archive_path_rejected_without_extraction(self):
        raw=rewrite_project(self.project,entries={'../outside.txt':b'bad'})
        with self.assertRaisesRegex(core.CreatorError,'not a Mirror Creator'):core.load_project(raw)

    def test_boolean_block_and_nonboolean_facing_rejected(self):
        for edit in (lambda p:p['selection'].update(block=True),lambda p:p['selection'].update(flip='false')):
            with self.assertRaises(core.CreatorError):core.load_project(rewrite_project(self.project,edit))

    def test_forged_plane_not_trusted(self):
        raw=rewrite_project(self.project,lambda p:p['selection'].update(normal=[9,9,9],origin=[999,999,999]))
        self.assertEqual(core.load_project(raw)[2],self.surface)


class HTTPTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server=CreatorServer(0);cls.thread=threading.Thread(target=cls.server.serve_forever,daemon=True);cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown();cls.server.server_close();cls.thread.join()

    def post(self,path,body,headers=None):
        return urlopen(Request(self.server.origin+path,data=body,headers=headers or {'X-Mirror-Creator':'1','Content-Type':'application/octet-stream'}),timeout=10)

    def test_import_select_save_open_through_http(self):
        with self.post('/api/import?name=own.nif',fixture()) as response:doc=json.load(response)['document']
        self.assertEqual(doc['name'],'own.nif')
        with self.post('/api/select',json.dumps(dict(id=doc['id'],block=1,seed=0)).encode()) as response:surface=json.load(response)
        with self.post('/api/save',json.dumps(dict(id=doc['id'],mirrorType='placeable',selection=surface)).encode()) as response:project=response.read()
        with self.post('/api/open-project',project) as response:restored=json.load(response)
        self.assertEqual(restored['selection'],surface);self.assertEqual(restored['mirrorType'],'placeable')

    def test_cross_origin_and_missing_local_header_rejected(self):
        for headers in ({'Content-Type':'text/plain'},{'X-Mirror-Creator':'1','Origin':'https://example.com'},{'X-Mirror-Creator':'1','Host':'evil.example'}):
            with self.subTest(headers=headers),self.assertRaises(HTTPError) as error:self.post('/api/import',fixture(),headers)
            self.assertEqual(error.exception.code,403)
            error.exception.close()

    def test_no_arbitrary_files_served(self):
        with self.assertRaises(HTTPError) as error:urlopen(self.server.origin+'/../HANDOVER.md')
        self.assertEqual(error.exception.code,404)
        error.exception.close()

    def test_requests_survive_closed_launcher_stdout(self):
        with patch('sys.stdout') as stream:
            stream.write.side_effect=BrokenPipeError('launcher exited')
            with urlopen(self.server.origin+'/api/health') as response:
                self.assertEqual(json.load(response)['app'],'Mirror Creator')

    def test_bad_file_returns_readable_error_and_server_survives(self):
        with self.assertRaises(HTTPError) as error:self.post('/api/import',b'not a nif')
        self.assertEqual(error.exception.code,400);self.assertIn('could not be read',json.load(error.exception)['error'])
        error.exception.close()
        with urlopen(self.server.origin+'/api/health') as response:self.assertFalse(json.load(response)['playableAddonExport'])


if __name__=='__main__':unittest.main()
