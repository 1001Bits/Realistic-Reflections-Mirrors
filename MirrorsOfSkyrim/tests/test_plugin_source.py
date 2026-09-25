"""The default editable records reproduce the 1.0 plugin exactly."""
from pathlib import Path
import hashlib
import json
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import build_plugin


class PluginSource(unittest.TestCase):
    def test_released_plugin_and_seq(self):
        plugin, seq = build_plugin.build()
        self.assertEqual(hashlib.sha256(plugin).hexdigest().upper(),
                         '802372FEF0E8D97AE1F347883CF668F5E00CF0F1BC2CD149B19914D40B1F05C3')
        self.assertEqual(seq, struct.pack('<I', 0x06000840))

    def test_core_master_and_start_quest(self):
        doc = json.loads((ROOT / 'records/MirrorsOfSkyrim.json').read_text())
        head = doc['nodes'][0]
        self.assertEqual(int(head['flags'], 16), 1)
        masters = [f['text'] for f in head['fields'] if f['type'] == 'MAST']
        self.assertEqual(masters, ['Skyrim.esm', 'Update.esm', 'HearthFires.esm',
                                  'Dawnguard.esm', 'Dragonborn.esm', 'RealisticReflectionsMirrors.esm'])

    def test_binary_and_text_fields(self):
        self.assertEqual(build_plugin.field_bytes({'type': 'EDID', 'text': 'Mirror'}), b'EDID\x07\x00Mirror\0')
        self.assertEqual(build_plugin.field_bytes({'type': 'DATA', 'hex': '0012FF'}), b'DATA\x03\x00\x00\x12\xFF')
        with self.assertRaises(ValueError):
            build_plugin.field_bytes({'type': 'DATA', 'text': 'both', 'hex': '00'})


if __name__ == '__main__':
    unittest.main()
