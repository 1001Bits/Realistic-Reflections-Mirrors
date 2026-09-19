"""CK identifiers follow source scene order, not block IDs or display names."""
import json
from pathlib import Path
import struct
import sys
import threading
import unittest
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import mirror_creator_core as core
import nif_block_edit as nif
from mirror_creator import CreatorServer
from test_mirror_creator import fixture
from test_mirror_creator_areas import separated_parts


class CKPartTests(unittest.TestCase):
    def test_nested_scene_order_includes_hidden_and_ignores_unused_blocks(self):
        raw, second = separated_parts()
        model = nif.parse(raw)
        root = nif.parse_node(model.blocks[0])
        nested = root.children[1]
        root.children = [nested, -1, 1]
        model.blocks[0] = nif.serialize_node(root)
        node = nif.parse_node(model.blocks[nested])
        node.av.flags |= 1
        model.blocks[nested] = nif.serialize_node(node)
        nif.append_block(model, 'BSTriShape', model.blocks[1])
        raw = nif.serialize(model)
        doc = core.load_nif(raw)
        self.assertEqual([(m.block, m.ck_index, m.ck_name, m.hidden) for m in doc.meshes],
                         [(second, 0, 'Pane', True), (1, 1, 'Pane', False)])
        self.assertEqual([m['ckIndex'] for m in doc.public()['meshes']], [0, 1])
        self.assertEqual(doc.data, raw)

    def test_unnamed_part_does_not_invent_a_ck_name(self):
        for name_index in (0xffffffff, 1):
            with self.subTest(name_index=name_index):
                model = nif.parse(fixture())
                model.strings[1] = ''
                shape = bytearray(model.blocks[1])
                struct.pack_into('<I', shape, 0, name_index)
                model.blocks[1] = bytes(shape)
                public = core.load_nif(nif.serialize(model)).public()['meshes'][0]
                self.assertEqual(public['name'], 'Part 1')
                self.assertEqual(public['ckName'], '')
                self.assertEqual(public['ckIndex'], 0)

    def test_raw_name_suffix_is_not_the_index(self):
        model = nif.parse(fixture())
        model.strings[1] = 'Window:<pane>&:93'
        public = core.load_nif(nif.serialize(model)).public()['meshes'][0]
        self.assertEqual(public['ckName'], 'Window:<pane>&:93')
        self.assertEqual(public['ckIndex'], 0)
        self.assertEqual(public['block'], 1)

    def test_http_import_exposes_identifiers_and_version_capability(self):
        with CreatorServer(0) as server:
            worker = threading.Thread(target=server.serve_forever, daemon=True)
            worker.start()
            try:
                with urlopen(server.origin+'/api/health', timeout=5) as response:
                    health = json.load(response)
                self.assertEqual(health['version'], '0.5')
                self.assertIs(health['ckPartIdentification'], True)
                request = Request(server.origin+'/api/import?name=part.nif', data=fixture(),
                                  headers={'X-Mirror-Creator': '1'})
                with urlopen(request, timeout=5) as response:
                    mesh = json.load(response)['document']['meshes'][0]
                self.assertEqual((mesh['ckName'], mesh['ckIndex']), ('Pane', 0))
            finally:
                server.shutdown()
                worker.join()


if __name__ == '__main__':
    unittest.main()
