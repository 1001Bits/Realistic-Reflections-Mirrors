"""Export a selected static Fallout pane without rewriting unrelated NIF blocks."""
from __future__ import annotations

import copy
import struct

import nif_block_edit as nif
from mirror_creator_core import (add, cross, dot, length, load_nif, matvec, mul,
                                 require, select_surface, sub, unit)
from mirror_creator_areas import combine_areas, selection_areas

PANE_NAME = 'MOFReflectiveSurface:0'
SCHEMA_NAME = 'MOFMirrorSurface'
SCHEMA_VALUE = '1'
MATERIAL = r'Materials\MirrorsOfFallout\Authoring\MOF_MirrorSurface.bgsm'
TEXTURES = 'Textures\\MirrorsOfFallout\\Authoring\\'
VERTEX_DESC = (0x41B << 44) | 0x650407  # float positions, UV, packed normal/tangent; 28 bytes


def _fallback_shader(model):
    paths = [TEXTURES+'mirror_surface.dds', TEXTURES+'mirror_surface_n.dds',
             '', '', '', '', '', TEXTURES+'mirror_surface_s.dds', '', '']
    textures = nif.append_block(model, 'BSShaderTextureSet', struct.pack('<I', len(paths)) +
                                b''.join(struct.pack('<I', len(p))+p.encode('ascii') for p in paths))
    # Fallout type-0 lighting property. The main mod's BGSM supplies the material;
    # inline values and textures also provide an opaque, unlit black fallback.
    shader = struct.pack('<IIIiII4fi4fII', 0, nif.string_index(model, MATERIAL), 0, -1,
                         0x80400200, 1, 0., 0., 1., 1., textures, 0., 0., 0., 1., 0xffffffff, 3)
    shader += struct.pack('<18f', 1., 0., 0., 1., 1., 1., 0., 0.,
                          3.4028234663852886e38, 0., 1., 5., *([-1.]*6))
    require(len(shader) == 140, 'Invalid Fallout shader layout.')
    return nif.append_block(model, 'BSLightingShaderProperty', shader)


def _replace_triangles(block, triangles):
    off = nif.parse_avobject(block).tail_offset
    desc, _, nv, _ = struct.unpack_from('<QIHI', block, off+28)
    stride = (desc & 15)*4
    header = bytearray(block[:off+46])
    struct.pack_into('<IHI', header, off+36, len(triangles), nv, nv*stride+len(triangles)*6)
    return bytes(header)+block[off+46:off+46+nv*stride]+b''.join(struct.pack('<3H', *t) for t in triangles)


def _mark_root(model, root):
    node = nif.parse_node(model.blocks[root])
    retained = []
    for ref in node.av.extra_data:
        require(0 <= ref < len(model.blocks) and len(model.blocks[ref]) >= 4, 'Invalid root extra data.')
        name = struct.unpack_from('<I', model.blocks[ref])[0]
        require(name == 0xffffffff or name < len(model.strings), 'Invalid extra-data name.')
        if name == 0xffffffff or model.strings[name] != SCHEMA_NAME:
            retained.append(ref)
    require(len(retained) < 64, 'The model root has too many extra-data entries.')
    marker = nif.append_block(model, 'NiStringExtraData',
                              struct.pack('<II', nif.string_index(model, SCHEMA_NAME), nif.string_index(model, SCHEMA_VALUE)))
    node.av.extra_data = retained+[marker]
    model.blocks[root] = nif.serialize_node(node)


def export_nif(doc, mirror_type, selection):
    require(mirror_type == 'placeable', 'Fallout Mirror Creator supports placed static mirrors.')
    combined = combine_areas(doc, selection_areas(selection))
    regions = combined['areas']
    model = nif.parse(doc.data)
    roots = struct.unpack_from(f'<{len(model.footer)//4-1}i', model.footer, 4)
    require(len(roots) == 1 and model.type_name(roots[0]) in ('NiNode', 'BSFadeNode'),
            'Use a model with one NiNode or BSFadeNode root.')
    chosen = {}
    for region in regions:
        chosen.setdefault(region['block'], set()).update(region['faces'])
    parents = {}
    loaded_parts = {mesh.block for mesh in doc.meshes}
    for index, block in enumerate(model.blocks):
        if model.type_name(index) in ('NiNode', 'BSFadeNode'):
            for child in nif.parse_node(block).children:
                if child in chosen:
                    require(child not in parents, 'A selected part has more than one scene parent.')
                    parents[child] = index
        elif model.type_name(index) == 'BSTriShape' and index in loaded_parts:
            av = nif.parse_avobject(block)
            name = model.strings[av.name] if av.name != 0xffffffff else ''
            shader = struct.unpack_from('<i', block, av.tail_offset+20)[0]
            prop_name = struct.unpack_from('<I', model.blocks[shader], 4)[0] if model.type_name(shader) == 'BSLightingShaderProperty' else 0xffffffff
            require(prop_name == 0xffffffff or prop_name < len(model.strings), 'The NIF has an invalid material name.')
            material = model.strings[prop_name].lower().replace('/', '\\') if prop_name != 0xffffffff else ''
            if name == PANE_NAME or material.endswith('mirrorsoffallout\\authoring\\mof_mirrorsurface.bgsm'):
                require(index in chosen and len(chosen[index]) == len(doc.mesh(index).triangles),
                        'This NIF already has a reflective part. Include its whole surface or start from the original NIF.')
    require(set(parents) == set(chosen), 'Every selected part must have a scene-node parent.')
    anchor = doc.mesh(regions[0]['block'])
    anchor_av = nif.parse_avobject(model.blocks[anchor.block])
    translation, rotation, scale = anchor.transform
    inverse = tuple(zip(*rotation))
    positions, triangles, indices, expected = [], [], {}, []
    for region in regions:
        mesh = doc.mesh(region['block'])
        for face in region['faces']:
            original = mesh.triangles[face]
            if region['flip']:
                original = (original[0], original[2], original[1])
            tri = []
            for index in original:
                world = mesh.positions[index]
                local = mul(matvec(inverse, sub(world, translation)), 1/scale)
                local = struct.unpack('<3f', struct.pack('<3f', *local))
                require(all(abs(v) <= 1e6 for v in local), 'The selected surface exceeds the supported coordinate range.')
                if local not in indices:
                    indices[local] = len(positions); positions.append(local)
                tri.append(indices[local]); expected.append(world)
            triangles.append(tuple(tri))
    require(0 < len(positions) <= 65535 and len(triangles) <= 65535, 'The selected surface exceeds the vertex/triangle limit.')

    # Existing identities and collision targets remain valid even when all their
    # render triangles move into the combined pane. No unrelated block is removed.
    for index, faces in chosen.items():
        block = model.blocks[index]; av = nif.parse_avobject(block)
        remaining = [t for i, t in enumerate(doc.mesh(index).triangles) if i not in faces]
        if remaining:
            model.blocks[index] = _replace_triangles(block, remaining)
        else:
            if av.name != 0xffffffff and model.strings[av.name] == PANE_NAME:
                av.name = nif.string_index(model, f'MOFSourceSurface:{index}')
            model.block_types[index] = nif.type_index(model, 'NiNode')
            model.blocks[index] = nif.serialize_node(nif.NodeLayout(av, [], []))

    a, b, c = (positions[v] for v in triangles[0])
    normal = unit(cross(sub(b, a), sub(c, a)))
    up = (0., 0., 1.) if abs(normal[2]) < .95 else (0., 1., 0.)
    right = unit(cross(up, normal)); up = unit(cross(normal, right))
    pack = lambda v: max(0, min(255, round((v+1)*127.5)))
    vertex_data = b''.join(struct.pack('<4f2e8B', *p, up[0], 0., 0.,
                          *map(pack, (*normal, up[1], *right, up[2]))) for p in positions)
    geometry = vertex_data+b''.join(struct.pack('<3H', *t) for t in triangles)
    center = tuple(sum(p[i] for p in positions)/len(positions) for i in range(3))
    radius = max(length(sub(p, center)) for p in positions)
    av = copy.deepcopy(anchor_av)
    av.name = nif.string_index(model, PANE_NAME)
    av.extra_data = []; av.collision = -1; av.flags &= ~1
    body = struct.pack('<4fiiiQIHI', *center, radius, -1, _fallback_shader(model), -1,
                       VERTEX_DESC, len(triangles), len(positions), len(geometry))
    pane = nif.append_block(model, 'BSTriShape', nif.serialize_avobject(av)+body+geometry)
    parent_index = parents[anchor.block]
    parent = nif.parse_node(model.blocks[parent_index]); parent.children.append(pane)
    model.blocks[parent_index] = nif.serialize_node(parent)
    _mark_root(model, roots[0])
    output = nif.serialize(model)
    verified = load_nif(output, doc.name)
    mesh = verified.mesh(pane)
    actual = [mesh.positions[v] for t in mesh.triangles for v in t]
    tolerance = max(1e-6, max(combined['width'], combined['height'])*1e-5)
    require(len(actual) == len(expected) and all(length(sub(a,b)) <= tolerance for a,b in zip(actual, expected)),
            'Combining these transforms loses surface precision. Apply the transforms in your model editor first.')
    checked = select_surface(verified, pane, mode='part', allow_disconnected=True)
    require(.02 <= min(checked['width']/scale, checked['height']/scale) and
            max(checked['width']/scale, checked['height']/scale) <= 10000,
            'The selected pane is too small or large for the mirror renderer.')
    return output, dict(schema=1, paneBlock=pane, sourceBlocks=list(chosen),
                        selectedAreas=len(regions), selectedTriangles=len(triangles),
                        originalUnchanged=True, mirrorType='placeable')
