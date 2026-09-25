"""Build MirrorsOfSkyrim.esp and its SEQ from the editable record source."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]


def signature(value: str) -> bytes:
    raw = value.encode('ascii')
    if len(raw) != 4:
        raise ValueError('A record or field signature must have four characters')
    return raw


def field_bytes(field: dict) -> bytes:
    if ('text' in field) == ('hex' in field):
        raise ValueError('Each field requires exactly one of text or hex')
    if 'text' in field:
        if '\0' in field['text']:
            raise ValueError('Text fields cannot contain embedded nulls')
        data = field['text'].encode('utf-8') + b'\0'
    else:
        data = bytes.fromhex(field['hex'])
    if len(data) > 0xFFFF:
        raise ValueError('This record source uses ordinary 16-bit subrecords')
    return signature(field['type']) + struct.pack('<H', len(data)) + data


def serialize(nodes: list[dict]) -> bytes:
    result = bytearray()
    for node in nodes:
        if 'group' in node:
            body = serialize(node['children'])
            label = signature(node['label']) if node['group'] == 0 else struct.pack('<I', int(node['label'], 16))
            result += struct.pack('<4sI4siHHHH', b'GRUP', 24 + len(body), label, node['group'],
                                  node['stamp'], node['unknown'], node['version'], node['unknown2'])
        else:
            body = b''.join(field_bytes(field) for field in node['fields'])
            flags = int(node['flags'], 16)
            if flags & 0x40000:
                raise ValueError('Compressed records are not used by this source')
            result += struct.pack('<4sIIIIHH', signature(node['record']), len(body), flags,
                                  int(node['formID'], 16), node['revision'], node['version'], node['unknown'])
        result += body
    return bytes(result)


def records(nodes):
    for node in nodes:
        if 'group' in node:
            yield from records(node['children'])
        else:
            yield node


def build(source: Path = ROOT / 'records/MirrorsOfSkyrim.json') -> tuple[bytes, bytes]:
    document = json.loads(source.read_text(encoding='utf-8'))
    if document['formatVersion'] != 1 or document['nodes'][0]['record'] != 'TES4':
        raise ValueError('Unsupported plugin record source')
    nodes = document['nodes']
    started_quests = []
    for record in records(nodes):
        if record['record'] == 'QUST':
            flags = next(field_bytes(f)[6:] for f in record['fields'] if f['type'] == 'DNAM')
            if struct.unpack_from('<H', flags)[0] & 1:
                started_quests.append(int(record['formID'], 16))
    seq = struct.pack('<' + 'I' * len(started_quests), *started_quests)
    return serialize(nodes), seq


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'records/MirrorsOfSkyrim.json')
    parser.add_argument('--output', type=Path, required=True, help='Output Data directory')
    args = parser.parse_args()
    plugin, seq = build(args.source)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'seq').mkdir(exist_ok=True)
    (args.output / 'MirrorsOfSkyrim.esp').write_bytes(plugin)
    (args.output / 'seq/MirrorsOfSkyrim.esp.seq').write_bytes(seq)
    print(f'Wrote MirrorsOfSkyrim.esp ({len(plugin)} bytes) and SEQ ({len(seq)} bytes)')
