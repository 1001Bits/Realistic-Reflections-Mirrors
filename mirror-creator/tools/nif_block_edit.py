#!/usr/bin/env python3
"""Minimal structural editor for the SSE NIF files this project generates.

Gamebryo 20.2.0.7 / Bethesda stream 100 files consist of a header (block
type table, per-block type index and size tables, string table), the block
payloads, and a root footer.  The mirror generators only ever need to append
whole blocks (``BSInvMarker``, ``NiAlphaProperty``), add a string, and rewrite
the few fixed-layout reference fields of ``NiNode``/``BSFadeNode`` and
``BSTriShape`` blocks.  Everything else is carried byte-for-byte, and
``serialize(parse(data)) == data`` is asserted before any edit.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


@dataclass
class Nif:
    header_line: bytes
    version: int
    endian: int
    user_version: int
    bs_version: int
    export_info: list[bytes]
    types: list[str]
    block_types: list[int]
    block_sizes: list[int]
    strings: list[str]
    max_string_length: int
    groups: list[int]
    blocks: list[bytes] = field(default_factory=list)
    footer: bytes = b""

    def type_name(self, index: int) -> str:
        return self.types[self.block_types[index]]


class _Reader:
    def __init__(self, data: bytes) -> None:
        self.data = data
        self.offset = 0

    def take(self, size: int) -> bytes:
        require(self.offset + size <= len(self.data), "NIF truncated")
        value = self.data[self.offset : self.offset + size]
        self.offset += size
        return value

    def unpack(self, fmt: str):
        size = struct.calcsize(fmt)
        return struct.unpack(fmt, self.take(size))


def parse(data: bytes) -> Nif:
    newline = data.index(b"\n") + 1
    reader = _Reader(data)
    header_line = reader.take(newline)
    require(
        header_line.startswith(b"Gamebryo File Format, Version 20.2.0.7"),
        "unsupported NIF header line",
    )
    (version,) = reader.unpack("<I")
    (endian,) = reader.unpack("<B")
    (user_version,) = reader.unpack("<I")
    (block_count,) = reader.unpack("<I")
    (bs_version,) = reader.unpack("<I")
    require(version == 0x14020007 and user_version == 12 and bs_version == 100,
            "unsupported NIF stream")
    export_info = []
    for _ in range(3):
        (length,) = reader.unpack("<B")
        export_info.append(reader.take(length))
    (type_count,) = reader.unpack("<H")
    types = []
    for _ in range(type_count):
        (length,) = reader.unpack("<I")
        types.append(reader.take(length).decode("ascii"))
    block_types = list(reader.unpack(f"<{block_count}H"))
    block_sizes = list(reader.unpack(f"<{block_count}I"))
    (string_count, max_string_length) = reader.unpack("<II")
    strings = []
    for _ in range(string_count):
        (length,) = reader.unpack("<I")
        strings.append(reader.take(length).decode("latin1"))
    (group_count,) = reader.unpack("<I")
    groups = list(reader.unpack(f"<{group_count}I"))
    blocks = [reader.take(size) for size in block_sizes]
    footer = data[reader.offset :]
    require(len(footer) == 4 + 4 * struct.unpack_from("<I", footer, 0)[0],
            "NIF footer size mismatch")
    nif = Nif(
        header_line, version, endian, user_version, bs_version, export_info,
        types, block_types, block_sizes, strings, max_string_length, groups,
        blocks, footer,
    )
    require(serialize(nif) == data, "NIF round-trip mismatch")
    return nif


def serialize(nif: Nif) -> bytes:
    out = bytearray(nif.header_line)
    out += struct.pack("<IBII", nif.version, nif.endian, nif.user_version,
                       len(nif.blocks))
    out += struct.pack("<I", nif.bs_version)
    for info in nif.export_info:
        out += struct.pack("<B", len(info)) + info
    out += struct.pack("<H", len(nif.types))
    for name in nif.types:
        encoded = name.encode("ascii")
        out += struct.pack("<I", len(encoded)) + encoded
    require(len(nif.block_types) == len(nif.blocks), "block type table mismatch")
    out += struct.pack(f"<{len(nif.blocks)}H", *nif.block_types)
    out += struct.pack(f"<{len(nif.blocks)}I", *[len(block) for block in nif.blocks])
    out += struct.pack("<II", len(nif.strings), nif.max_string_length)
    for value in nif.strings:
        encoded = value.encode("latin1")
        out += struct.pack("<I", len(encoded)) + encoded
    out += struct.pack("<I", len(nif.groups))
    if nif.groups:
        out += struct.pack(f"<{len(nif.groups)}I", *nif.groups)
    for block in nif.blocks:
        out += block
    out += nif.footer
    return bytes(out)


def string_index(nif: Nif, value: str) -> int:
    """Return the index of ``value`` in the string table, appending if absent."""
    if value in nif.strings:
        return nif.strings.index(value)
    nif.strings.append(value)
    nif.max_string_length = max(nif.max_string_length, len(value.encode("latin1")))
    return len(nif.strings) - 1


def type_index(nif: Nif, name: str) -> int:
    if name in nif.types:
        return nif.types.index(name)
    nif.types.append(name)
    return len(nif.types) - 1


def append_block(nif: Nif, type_name: str, payload: bytes) -> int:
    nif.blocks.append(bytes(payload))
    nif.block_types.append(type_index(nif, type_name))
    nif.block_sizes.append(len(payload))
    return len(nif.blocks) - 1


def find_blocks(nif: Nif, type_name: str) -> list[int]:
    return [index for index in range(len(nif.blocks)) if nif.type_name(index) == type_name]


# --- NiObjectNET / NiAVObject / NiNode / BSTriShape fixed-layout helpers ----

@dataclass
class AVObjectLayout:
    name: int
    extra_data: list[int]
    controller: int
    flags: int
    transform: bytes  # translation(12) rotation(36) scale(4)
    collision: int
    tail_offset: int  # offset of the first field after collision


def parse_avobject(block: bytes) -> AVObjectLayout:
    name, extra_count = struct.unpack_from("<Ii", block, 0)
    require(0 <= extra_count <= 64, "unreasonable extra-data count")
    offset = 8
    extras = list(struct.unpack_from(f"<{extra_count}i", block, offset))
    offset += 4 * extra_count
    controller, flags = struct.unpack_from("<iI", block, offset)
    offset += 8
    transform = block[offset : offset + 52]
    offset += 52
    (collision,) = struct.unpack_from("<i", block, offset)
    offset += 4
    return AVObjectLayout(name, extras, controller, flags, transform, collision, offset)


def set_avobject_translation(block: bytes, translation: tuple[float, float, float]) -> bytes:
    """Rewrite only the NiAVObject translation of a node/shape block."""
    layout = parse_avobject(block)
    offset = 8 + 4 * len(layout.extra_data) + 8
    out = bytearray(block)
    struct.pack_into("<3f", out, offset, *translation)
    return bytes(out)


def set_avobject_scale(block: bytes, scale: float) -> bytes:
    """Rewrite only the NiAVObject scale of a node/shape block."""
    layout = parse_avobject(block)
    offset = 8 + 4 * len(layout.extra_data) + 8 + 48
    out = bytearray(block)
    struct.pack_into("<f", out, offset, scale)
    return bytes(out)


def serialize_avobject(layout: AVObjectLayout) -> bytes:
    out = struct.pack("<Ii", layout.name, len(layout.extra_data))
    if layout.extra_data:
        out += struct.pack(f"<{len(layout.extra_data)}i", *layout.extra_data)
    out += struct.pack("<iI", layout.controller, layout.flags)
    out += layout.transform
    out += struct.pack("<i", layout.collision)
    return out


@dataclass
class NodeLayout:
    av: AVObjectLayout
    children: list[int]
    effects: list[int]


def parse_node(block: bytes) -> NodeLayout:
    av = parse_avobject(block)
    offset = av.tail_offset
    (child_count,) = struct.unpack_from("<I", block, offset)
    offset += 4
    children = list(struct.unpack_from(f"<{child_count}i", block, offset))
    offset += 4 * child_count
    (effect_count,) = struct.unpack_from("<I", block, offset)
    offset += 4
    effects = list(struct.unpack_from(f"<{effect_count}i", block, offset))
    offset += 4 * effect_count
    require(offset == len(block), "NiNode block has trailing bytes")
    return NodeLayout(av, children, effects)


def serialize_node(node: NodeLayout) -> bytes:
    out = bytearray(serialize_avobject(node.av))
    out += struct.pack("<I", len(node.children))
    if node.children:
        out += struct.pack(f"<{len(node.children)}i", *node.children)
    out += struct.pack("<I", len(node.effects))
    if node.effects:
        out += struct.pack(f"<{len(node.effects)}i", *node.effects)
    return bytes(out)


# BSTriShape after the NiAVObject collision reference: bound (16), skin (4),
# shader property (4), alpha property (4), then the vertex descriptor.
TRISHAPE_ALPHA_OFFSET_AFTER_COLLISION = 16 + 4 + 4


def trishape_alpha_offset(block: bytes) -> int:
    av = parse_avobject(block)
    return av.tail_offset + TRISHAPE_ALPHA_OFFSET_AFTER_COLLISION


def trishape_shader_offset(block: bytes) -> int:
    av = parse_avobject(block)
    return av.tail_offset + 16 + 4


def set_trishape_alpha(block: bytes, alpha_block: int) -> bytes:
    offset = trishape_alpha_offset(block)
    require(struct.unpack_from("<i", block, offset)[0] == -1,
            "shape already owns an alpha property")
    out = bytearray(block)
    struct.pack_into("<i", out, offset, alpha_block)
    return bytes(out)


def build_inv_marker(name_index: int, rotation_x: int, rotation_y: int,
                     rotation_z: int, zoom: float) -> bytes:
    """``BSInvMarker``: name, three uint16 clockwise milliradians, float zoom."""
    for value in (rotation_x, rotation_y, rotation_z):
        require(0 <= value <= 0xFFFF, "inventory marker rotation out of range")
    require(zoom > 0.0, "inventory marker zoom must be positive")
    return struct.pack("<IHHHf", name_index, rotation_x, rotation_y, rotation_z, zoom)


def build_alpha_property(flags: int, threshold: int) -> bytes:
    """``NiAlphaProperty``: unnamed, no extra data or controller."""
    require(0 <= flags <= 0xFFFF and 0 <= threshold <= 0xFF, "alpha fields out of range")
    return struct.pack("<IiiHB", 0xFFFFFFFF, 0, -1, flags, threshold)


# BSLightingShaderProperty fixed field offsets (SSE stream 100, shader type
# leads the block).
LSP_SHADER_TYPE = 0
LSP_FLAGS1 = 16
LSP_FLAGS2 = 20
LSP_EMISSIVE_MULTIPLE = 56
LSP_ALPHA = 64
LSP_GLOSSINESS = 72
LSP_SPECULAR_COLOR = 76
LSP_SPECULAR_STRENGTH = 88


def set_float(block: bytes, offset: int, value: float) -> bytes:
    out = bytearray(block)
    struct.pack_into("<f", out, offset, value)
    return bytes(out)
