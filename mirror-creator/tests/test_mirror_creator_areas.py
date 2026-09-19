"""Coplanar multi-part selection, exact gaps, transform and collision retention."""
import copy
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import mirror_creator_core as core
import mirror_creator_areas as areas
import mirror_creator_export as export
import nif_block_edit as nif
from test_mirror_creator import fixture
from test_mirror_creator_export import metadata


def separated_parts(offset=0., tilted=False, reverse=False, transformed=False):
    raw = fixture(translation=(10,20,30) if transformed else (0,0,0),
                  rotation=(0,0,1,0,1,0,-1,0,0) if transformed else None,
                  scale=-2 if transformed else 1)
    model = nif.parse(raw)
    shape = model.blocks[1]
    if reverse:
        shape = export._replace_triangles(shape, [(0,2,1),(0,3,2)])
    second = nif.append_block(model, 'BSTriShape', shape)
    av = copy.deepcopy(nif.parse_node(model.blocks[0]).av)
    av.name = nif.string_index(model, 'Other part transform')
    rotation = (0,-.8660254037844386,.5,1,0,0,0,.5,.8660254037844386) if tilted else (0,-1,0,1,0,0,0,0,1)
    av.transform = struct.pack('<13f', 8,0,offset,*rotation,1)
    child = nif.append_block(model, 'NiNode', nif.serialize_node(nif.NodeLayout(av,[second],[])))
    root = nif.parse_node(model.blocks[0]); root.children.append(child)
    model.blocks[0] = nif.serialize_node(root)
    return nif.serialize(model), second


def broken_fixture():
    # Two separate flat quadrilaterals with a clear gap, and one bent frame face.
    return fixture([(-3,-2,0),(-.4,-2,0),(-.4,2,0),(-3,2,0),
                    (.4,-2,0),(3,-2,0),(3,2,0),(.4,2,0),(-3,-2,1)],
                   [(0,1,2),(0,2,3),(4,5,6),(4,6,7),(0,8,3)])


class AreaTests(unittest.TestCase):
    def combined(self, raw, second=1, seed=2, flip=False):
        doc = core.load_nif(raw)
        first = core.select_surface(doc,1,0)
        selected = areas.toggle_area(doc,first,core.select_surface(doc,second,seed,flip=flip))
        return doc, selected

    def test_disconnected_areas_keep_real_gap_and_one_declaration(self):
        doc, selected = self.combined(broken_fixture())
        self.assertEqual(selected['triangleCount'],4)
        self.assertAlmostEqual(selected['area'],20.8)
        raw, report = export.export_nif(doc,'placeable',selected)
        model, pane, tags = metadata(raw)
        self.assertEqual(len(tags[export.TRIANGLES_NAME]),24)
        self.assertEqual(tags[export.SCHEMA_NAME],1)
        loaded = core.load_nif(raw)
        self.assertEqual(len(loaded.mesh(1).triangles),1)  # bent frame retained
        self.assertEqual(loaded.mesh(1).positions,doc.mesh(1).positions)
        for triangle in loaded.mesh(pane).triangles:
            xs = [loaded.mesh(pane).positions[v][0] for v in triangle]
            self.assertTrue(max(xs) <= -.39999 or min(xs) >= .39999, 'A triangle fills the gap')
        self.assertEqual(report['selectedTriangles'],4)

    def test_toggle_remove_last_and_shift_first(self):
        doc = core.load_nif(broken_fixture()); left=core.select_surface(doc,1,0);right=core.select_surface(doc,1,2)
        self.assertEqual(areas.toggle_area(doc,None,left),left)
        together=areas.toggle_area(doc,left,right)
        only_right=areas.toggle_area(doc,together,left)
        self.assertEqual(only_right['areas'][0]['faces'],right['faces'])
        self.assertIsNone(areas.toggle_area(doc,only_right,right))
        self.assertEqual(areas.toggle_area(doc,only_right,left)['triangleCount'],4)

    def test_different_parts_nested_transforms_and_negative_scale(self):
        raw, second=separated_parts(transformed=True)
        doc, selected=self.combined(raw,second,0)
        expected=[p for r in selected['areas'] for f in r['faces'] for p in doc.mesh(r['block']).face(f)]
        output, report=export.export_nif(doc,'hand',selected)
        model,pane,tags=metadata(output); loaded=core.load_nif(output)
        actual=[p for f in range(4) for p in loaded.mesh(pane).face(f)]
        for a,b in zip(actual,expected):
            for x,y in zip(a,b):self.assertAlmostEqual(x,y,places=5)
        self.assertEqual(tags[export.PROFILE_NAME],2)
        self.assertEqual(report['sourceBlocks'],[1,second])

    def test_opposite_source_windings_can_share_clicked_facing(self):
        raw,second=separated_parts(reverse=True)
        doc,selected=self.combined(raw,second,0,flip=True)
        output,_=export.export_nif(doc,'placeable',selected)
        _,_,tags=metadata(output)
        self.assertEqual(tags[export.PLANE_NAME][3:6],(0,0,1))

    def test_tilted_offset_and_opposite_facing_are_rejected_without_mutation(self):
        for kwargs in (dict(offset=1),dict(tilted=True),dict(reverse=True)):
            raw,second=separated_parts(**kwargs)
            doc=core.load_nif(raw);first=core.select_surface(doc,1,0);before=copy.deepcopy(first)
            with self.assertRaisesRegex(core.CreatorError,'separate reflections'):
                areas.toggle_area(doc,first,core.select_surface(doc,second,0))
            self.assertEqual(first,before);self.assertEqual(doc.data,raw)

    def test_collision_owners_and_unrelated_blocks_survive_full_part_consumption(self):
        raw,second=separated_parts();model=nif.parse(raw)
        collision=nif.append_block(model,'CollisionEvidence',b'unchanged native collision payload')
        extra=nif.append_block(model,'NiIntegerExtraData',struct.pack('<Ii',nif.string_index(model,'AuthorTag'),42))
        avs={}
        for index in (1,second):
            av=nif.parse_avobject(model.blocks[index]);tail=model.blocks[index][av.tail_offset:]
            av.collision=collision;av.extra_data=[extra];avs[index]=av
            model.blocks[index]=nif.serialize_avobject(av)+tail
        doc,selected=self.combined(nif.serialize(model),second,0)
        output,_=export.export_nif(doc,'placeable',selected);result=nif.parse(output)
        self.assertEqual(result.blocks[collision],model.blocks[collision])
        self.assertEqual(result.blocks[extra],model.blocks[extra])
        for index,av in avs.items():
            node=nif.parse_node(result.blocks[index])
            self.assertEqual(result.type_name(index),'NiNode')
            self.assertEqual(nif.serialize_avobject(node.av),nif.serialize_avobject(av))
            self.assertEqual(node.children,[])

    def test_reopening_and_combining_saved_shards_keeps_one_marker(self):
        doc,selected=self.combined(broken_fixture())
        first,report=export.export_nif(doc,'placeable',selected);loaded=core.load_nif(first)
        pane=report['paneBlock'];s=areas.toggle_area(loaded,core.select_surface(loaded,pane,0),core.select_surface(loaded,pane,2))
        output,_=export.export_nif(loaded,'placeable',s)
        _,_,tags=metadata(output)
        self.assertEqual(len(tags[export.TRIANGLES_NAME]),24)

    def test_saved_derived_plane_is_not_trusted(self):
        doc,selected=self.combined(broken_fixture());expected=export.export_nif(doc,'hand',selected)[0]
        selected['normal']=[0,1,0];selected['origin']=[999,999,999]
        self.assertEqual(export.export_nif(doc,'hand',selected)[0],expected)

    def test_duplicate_missing_hidden_and_invalid_regions_rejected(self):
        doc=core.load_nif(broken_fixture());s=core.select_surface(doc,1,0)
        with self.assertRaises(core.CreatorError):areas.combine_areas(doc,[s,s])
        for invalid in ([],[None],[dict(block=99,faces=[0])],[dict(block=1,faces=[True])]):
            with self.assertRaises(core.CreatorError):areas.combine_areas(doc,invalid)
        doc.mesh(1).hidden=True
        with self.assertRaisesRegex(core.CreatorError,'Hidden'):areas.combine_areas(doc,[s])


if __name__=='__main__':unittest.main()
