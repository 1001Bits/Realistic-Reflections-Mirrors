"""Lossless preparation and runtime metadata integration checks."""
import json
from pathlib import Path
import struct
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import mirror_creator_core as core
import mirror_creator_export as export
import nif_block_edit as nif
import test_mirror_creator as preview_tests
fixture=preview_tests.fixture


def metadata(raw):
    model=nif.parse(raw)
    panes=[i for i in range(len(model.blocks)) if model.type_name(i)=='BSTriShape' and
           model.strings[nif.parse_avobject(model.blocks[i]).name]==export.PANE_NAME]
    assert len(panes)==1
    index=panes[0]; av=nif.parse_avobject(model.blocks[index]); values={}
    for ref in av.extra_data:
        block=model.blocks[ref];name=model.strings[struct.unpack_from('<I',block)[0]]
        if model.type_name(ref)=='NiFloatsExtraData':
            count=struct.unpack_from('<I',block,4)[0]
            values[name]=struct.unpack_from(f'<{count}f',block,8)
        elif model.type_name(ref)=='NiIntegerExtraData':
            values[name]=struct.unpack_from('<i',block,4)[0]
    return model,index,values


class ExportTests(unittest.TestCase):
    def prepare(self,raw=None,block=1,flip=False,kind='placeable'):
        doc=core.load_nif(raw or fixture(),'custom.nif')
        surface=core.select_surface(doc,block,seed=0,flip=flip)
        output,report=export.export_nif(doc,kind,surface)
        return doc,surface,output,report

    def test_selected_vertices_reconstructed_exactly_from_metadata(self):
        for flip in (False,True):
            # Asymmetric triangle proves the bounding box center is used instead
            # of the area centroid. The model's local origin is not on its pane.
            raw=fixture([(1,3,7),(9,3,7),(1,11,7)],[(0,1,2)],translation=(12,43,21),
                        rotation=(0,0,1,0,1,0,-1,0,0),scale=-2)
            original,surface,output,report=self.prepare(raw,flip=flip)
            _,_,tags=metadata(output);p=tags[export.PLANE_NAME];uv=tags[export.TRIANGLES_NAME]
            positions=[(1,3,7),(9,3,7),(1,11,7)]
            if flip:positions=[positions[0],positions[2],positions[1]]
            for i,expected in enumerate(positions):
                actual=[p[a]+p[6+a]*uv[i*2]*p[12]+p[9+a]*uv[i*2+1]*p[13] for a in range(3)]
                for a,b in zip(actual,expected):self.assertAlmostEqual(a,b,places=5)
            self.assertEqual(original.data,raw)
            self.assertEqual(len(core.load_nif(output).meshes),1)

    def test_partial_mesh_split_preserves_unselected_triangles_attributes_and_transform(self):
        raw=fixture([(0,0,0),(2,0,0),(2,3,0),(0,3,0),(2,3,2)],[(0,1,2),(0,2,3),(1,4,2)],translation=(12,9,4),scale=3)
        doc,selection,output,report=self.prepare(raw)
        loaded=core.load_nif(output);pane=loaded.mesh(report['paneBlock']);remaining=loaded.mesh(1)
        self.assertEqual(remaining.triangles,[(1,4,2)])
        self.assertEqual(pane.triangles,[(0,1,2),(0,2,3)])
        self.assertEqual(pane.positions,doc.mesh(1).positions)
        self.assertEqual(remaining.positions,doc.mesh(1).positions)
        model=nif.parse(output);a=nif.parse_avobject(model.blocks[1]);b=nif.parse_avobject(model.blocks[report['paneBlock']])
        self.assertEqual(a.transform,b.transform)
        self.assertEqual(nif.parse_node(model.blocks[0]).children,[1,report['paneBlock']])

    def test_hole_and_concavity_are_actual_triangle_contours(self):
        v=[(0,0,0),(4,0,0),(4,4,0),(0,4,0),(1,1,0),(3,1,0),(3,3,0),(1,3,0)]
        t=[(0,1,5),(0,5,4),(1,2,6),(1,6,5),(2,3,7),(2,7,6),(3,0,4),(3,4,7)]
        _,_,output,report=self.prepare(fixture(v,t));_,_,tags=metadata(output)
        self.assertEqual(len(tags[export.TRIANGLES_NAME]),48)
        s=core.select_surface(core.load_nif(output),report['paneBlock'],seed=0)
        self.assertEqual(len(s['boundary']),8);self.assertAlmostEqual(s['area'],12)

    def test_real_round_and_standing_models_reimport(self):
        for path,name,count in [(ROOT/'dist/Data/meshes/mirrors_of_skyrim/mirror01.nif','TrueMirror:0',2),
                                (ROOT/'prototypes/hand-mirror/gilded-noble-round-filigree-v1/nifs/male-world.nif','RRMirrorFallback:0',24)]:
            raw=path.read_bytes();doc=core.load_nif(raw,path.name);block=next(m.block for m in doc.meshes if m.name==name)
            _,_,out,report=self.prepare(raw,block)
            model,pane,tags=metadata(out);old=nif.parse(raw)
            self.assertEqual(report['selectedTriangles'],count)
            for i,data in enumerate(old.blocks):
                if i!=block:self.assertEqual(model.blocks[i],data)
            self.assertEqual(len(tags[export.TRIANGLES_NAME]),count*6)
            self.assertEqual(path.read_bytes(),raw)

    def test_metadata_is_local_versioned_and_deterministic(self):
        doc,surface,output,_=self.prepare()
        self.assertEqual(export.export_nif(doc,'placeable',surface)[0],output)
        for kind,number in [('placeable',1),('hand',2)]:
            prepared,_=export.export_nif(doc,kind,surface);model,pane,tags=metadata(prepared)
            self.assertEqual(tags[export.SCHEMA_NAME],1);self.assertEqual(tags[export.PROFILE_NAME],number)
            self.assertEqual(len(tags[export.PLANE_NAME]),14)
            av=nif.parse_avobject(model.blocks[pane]);shader,alpha=struct.unpack_from('<ii',model.blocks[pane],av.tail_offset+20)
            self.assertEqual(alpha,-1);self.assertEqual(model.type_name(shader),'BSLightingShaderProperty')
            self.assertEqual(len(model.blocks[shader]),104)

    def test_collision_stays_with_original_when_split(self):
        model=nif.parse(fixture([(0,0,0),(2,0,0),(2,3,0),(0,3,0),(2,3,2)],[(0,1,2),(0,2,3),(1,4,2)]))
        collision=nif.append_block(model,'UnknownCollisionEvidence',b'preserve-me')
        av=nif.parse_avobject(model.blocks[1]);tail=model.blocks[1][av.tail_offset:];av.collision=collision
        model.blocks[1]=nif.serialize_avobject(av)+tail
        _,_,output,report=self.prepare(nif.serialize(model));model=nif.parse(output)
        self.assertEqual(nif.parse_avobject(model.blocks[1]).collision,collision)
        self.assertEqual(nif.parse_avobject(model.blocks[report['paneBlock']]).collision,-1)
        self.assertEqual(model.blocks[collision],b'preserve-me')

    def test_reexport_keeps_a_single_marker(self):
        _,_,output,_=self.prepare();model,pane,_=metadata(output)
        doc=core.load_nif(output);selection=core.select_surface(doc,pane,seed=0,flip=True)
        again,_=export.export_nif(doc,'placeable',selection)
        model,index,tags=metadata(again)
        av=nif.parse_avobject(model.blocks[index])
        self.assertEqual(len(av.extra_data),4)
        self.assertEqual(tags[export.PLANE_NAME][5],-1)


class ExportHTTPTests(unittest.TestCase):
    setUpClass=classmethod(preview_tests.HTTPTests.setUpClass.__func__)
    tearDownClass=classmethod(preview_tests.HTTPTests.tearDownClass.__func__)
    post=preview_tests.HTTPTests.post
    def test_http_export_uses_saved_faces_not_forged_derived_plane(self):
        with self.post('/api/import?name=own.nif',fixture()) as response:doc=json.load(response)['document']
        selection=dict(block=1,faces=[0],flip=False,normal=[999,999,999])
        with self.post('/api/export-nif',json.dumps(dict(id=doc['id'],mirrorType='placeable',selection=selection)).encode()) as response:
            output=response.read()
        model,pane,tags=metadata(output)
        self.assertEqual(len(tags[export.TRIANGLES_NAME]),6)
        self.assertEqual(tags[export.PLANE_NAME][3:6],(0,0,1))


if __name__=='__main__': unittest.main()
