"""Prepare the selected NIF surface for model-driven mirror recognition.

Only selected triangle indices become the pane. Original vertex streams,
transforms, unselected geometry and unrelated blocks are preserved.
"""
from __future__ import annotations

import copy
import math
import struct

import nif_block_edit as nif
from mirror_creator_core import (Document, Mesh, CreatorError, add, dot, filename,
                                 length, load_nif, mul, require, select_surface, sub)

PANE_NAME = 'MOSReflectiveSurface:0'
SCHEMA_NAME = 'MOSMirrorSurface'
PLANE_NAME = 'MOSMirrorPlane'
TRIANGLES_NAME = 'MOSMirrorTriangles'
PROFILE_NAME = 'MOSMirrorProfile'
SCHEMA_VERSION = 1
PLANE_VALUE_COUNT = 14


def _float_block(model, name, values):
    return nif.append_block(model, 'NiFloatsExtraData',
                            struct.pack('<II', nif.string_index(model, name), len(values))+
                            struct.pack(f'<{len(values)}f', *values))


def _integer_block(model, name, value):
    return nif.append_block(model, 'NiIntegerExtraData',
                            struct.pack('<Ii', nif.string_index(model, name), value))


def _fallback_shader(model):
    # The existing first-party opaque black pane's SSE lighting material.
    # Environment/specular strengths are zero; all paths are vanilla textures.
    paths = [b'textures\\Black.dds', b'textures\\defaultwithspec_n.dds', b'', b'',
             b'textures\\cubemaps\\ShinyBright_e.dds', b'textures\\Black.dds', b'', b'', b'']
    textures = nif.append_block(model, 'BSShaderTextureSet', struct.pack('<I', 9)+
                                b''.join(struct.pack('<I', len(p))+p for p in paths))
    shader = bytearray.fromhex(
        '01000000ffffffff00000000ffffffff81034082118000000000000000000000'
        '0000803f0000803f060000000000000000000000000000000000000003000000'
        '0000803f000000000000a0420000803f0000803f0000803f000000009a99993e'
        '0000004000000000')
    struct.pack_into('<i', shader, 40, textures)
    return nif.append_block(model, 'BSLightingShaderProperty', bytes(shader))


def _replace_triangles(block, triangles):
    av = nif.parse_avobject(block); off = av.tail_offset
    desc, _, nv, _ = struct.unpack_from('<QHHI', block, off+28)
    stride = (desc & 15)*4
    header = bytearray(block[:off+44])
    vertices = block[off+44:off+44+nv*stride]
    struct.pack_into('<HHI', header, off+36, len(triangles), nv, nv*stride+len(triangles)*6)
    return bytes(header)+vertices+b''.join(struct.pack('<3H', *t) for t in triangles)+struct.pack('<I', 0)


def export_nif(doc: Document, mirror_type: str, selection: dict, *, _allow_disconnected=False):
    require(mirror_type in ('hand', 'placeable'), 'Choose Hand mirror or Placeable mirror.')
    require(isinstance(selection, dict), 'Select a reflective surface before exporting.')
    if 'areas' in selection:
        from mirror_creator_areas import export_areas
        return export_areas(doc, mirror_type, selection)
    surface = select_surface(doc, selection.get('block'), mode='saved',
                             faces=selection.get('faces'), flip=selection.get('flip', False),
                             allow_disconnected=_allow_disconnected)
    model = nif.parse(doc.data)
    source_index = surface['block']; block = model.blocks[source_index]
    source_av = nif.parse_avobject(block); off = source_av.tail_offset
    desc, nt, nv, size = struct.unpack_from('<QHHI', block, off+28)
    stride = (desc & 15)*4
    positions = [struct.unpack_from('<3f', block, off+44+v*stride) for v in range(nv)]
    triangles = list(struct.iter_unpack('<3H', block[off+44+nv*stride:off+44+size]))
    local_doc = Document(doc.data, doc.name, [Mesh(source_index, surface['name'], positions, triangles)], [])
    local = select_surface(local_doc, source_index, mode='saved', faces=surface['faces'], flip=surface['flip'],
                           allow_disconnected=_allow_disconnected)
    chosen = set(surface['faces'])
    pane_triangles = [triangles[i] for i in surface['faces']]
    if surface['flip']:
        pane_triangles = [(a,c,b) for a,b,c in pane_triangles]

    # Area centroid need not be the aperture bounding-box center (e.g. a triangle).
    points = [positions[v] for t in pane_triangles for v in t]
    us = [dot(sub(p, local['origin']), local['right']) for p in points]
    vs = [dot(sub(p, local['origin']), local['up']) for p in points]
    center = add(local['origin'], add(mul(local['right'], (min(us)+max(us))*.5),
                                      mul(local['up'], (min(vs)+max(vs))*.5)))
    half_width = (max(us)-min(us))*.5; half_height = (max(vs)-min(vs))*.5
    require(min(half_width, half_height) > 1e-4 and max(half_width, half_height) <= 1e6,
            'The selected pane is too small or too large for runtime reflection.')
    plane = [*center, *local['normal'], *local['right'], *local['up'], half_width, half_height]
    require(all(math.isfinite(v) and abs(v) <= 1e6 for v in plane),
            'The selected pane coordinates exceed the runtime surface range.')
    units = [c for p in points for c in (dot(sub(p, center),local['right'])/half_width,
                                        dot(sub(p, center),local['up'])/half_height)]
    require(all(math.isfinite(v) and abs(v) <= 1.00001 for v in units), 'The selected outline could not be normalized.')
    # Validate the float32 values Skyrim will actually load, not just Python's
    # double-precision working coordinates. Very thin triangles can collapse.
    encoded_units = struct.unpack(f'<{len(units)}f', struct.pack(f'<{len(units)}f', *units))
    area = 0.0
    for i in range(0, len(encoded_units), 6):
        ax,ay,bx,by,cx,cy = encoded_units[i:i+6]
        twice = (bx-ax)*(cy-ay)-(by-ay)*(cx-ax)
        require(twice > 0, 'A selected triangle collapses at runtime precision. Simplify the pane and try again.')
        area += twice*.5
    require(area <= 4.0001, 'The selected triangles overlap beyond the pane bounds.')

    # A NIF carries one reflecting surface in this schema. Never silently alter
    # another explicitly prepared surface in the imported asset.
    for i, other in enumerate(model.blocks):
        if i != source_index and model.type_name(i) == 'BSTriShape':
            av = nif.parse_avobject(other)
            require(av.name == 0xffffffff or model.strings[av.name] != PANE_NAME,
                    'This NIF already has another prepared reflecting surface. Select that surface or use an unprepared model.')
    parents = []
    for i, other in enumerate(model.blocks):
        if model.type_name(i) in ('NiNode','BSFadeNode'):
            node = nif.parse_node(other)
            if source_index in node.children:
                parents.append((i,node))
    require(len(parents) == 1, 'The selected part must have exactly one scene-node parent.')

    remaining = [t for i,t in enumerate(triangles) if i not in chosen]
    pane_block = _replace_triangles(block, pane_triangles)
    pane_av = copy.deepcopy(source_av)
    pane_av.name = nif.string_index(model, PANE_NAME)
    pane_av.flags &= ~1  # The explicitly selected surface is visible.
    if remaining:
        pane_av.collision = -1  # Collision remains with the original object.
        model.blocks[source_index] = _replace_triangles(block, remaining)
    # Preserve unrelated extra data, replacing only this tool's own declarations.
    owned_names = {SCHEMA_NAME, PLANE_NAME, TRIANGLES_NAME, PROFILE_NAME}
    retained = []
    for ref in pane_av.extra_data:
        require(0 <= ref < len(model.blocks), 'The selected part has an invalid extra-data reference.')
        extra = model.blocks[ref]
        name_index = struct.unpack_from('<I',extra)[0]
        name = model.strings[name_index] if name_index < len(model.strings) else ''
        if name not in owned_names:
            retained.append(ref)
    pane_av.extra_data = retained + [
        _integer_block(model, SCHEMA_NAME, SCHEMA_VERSION),
        _float_block(model, PLANE_NAME, plane),
        _float_block(model, TRIANGLES_NAME, units),
        _integer_block(model, PROFILE_NAME, 1 if mirror_type == 'placeable' else 2),
    ]
    require(len(pane_av.extra_data) <= 64, 'The selected part has too many extra-data entries.')
    tail = bytearray(pane_block[source_av.tail_offset:])
    center_bound = tuple(sum(p[i] for p in points)/len(points) for i in range(3))
    radius = max(length(sub(p, center_bound)) for p in points)
    struct.pack_into('<4f',tail,0,*center_bound,radius)
    struct.pack_into('<i',tail,20,_fallback_shader(model))
    struct.pack_into('<i',tail,24,-1)  # Opaque fallback, no original alpha blending.
    prepared_pane = nif.serialize_avobject(pane_av)+bytes(tail)
    if remaining:
        pane_index = nif.append_block(model,'BSTriShape',prepared_pane)
        parent_index, parent = parents[0]
        parent.children.insert(parent.children.index(source_index)+1,pane_index)
        model.blocks[parent_index] = nif.serialize_node(parent)
    else:
        pane_index = source_index
        model.blocks[source_index] = prepared_pane
    output = nif.serialize(model)
    verified = load_nif(output, filename(doc.name))
    prepared = verified.mesh(pane_index)
    expected = [doc.mesh(source_index).positions[v] for t in pane_triangles for v in t]
    actual = [prepared.positions[v] for t in prepared.triangles for v in t]
    require(actual == expected, 'Export changed the selected surface placement; no NIF was written.')
    return output, dict(schema=SCHEMA_VERSION, paneBlock=pane_index, sourceBlock=source_index,
                        selectedTriangles=len(pane_triangles), remainingTriangles=len(remaining),
                        mirrorType=mirror_type, plane=plane, originalUnchanged=True)
