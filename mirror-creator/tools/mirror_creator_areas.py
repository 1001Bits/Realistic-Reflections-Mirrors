"""One planar reflection spanning explicitly selected, possibly separate areas."""
from __future__ import annotations

import copy
import math
import struct

import nif_block_edit as nif
from mirror_creator_core import (Document, add, cross, dot, length, load_nif,
                                 matvec, mul, require, select_surface, sub, unit)


def selection_areas(selection):
    require(isinstance(selection, dict), 'Select a reflective area first.')
    areas = selection.get('areas', [selection])
    require(isinstance(areas, list) and 0 < len(areas) <= 8192,
            'The area selection is empty or too large.')
    return areas


def combine_areas(doc: Document, areas):
    require(isinstance(areas, list) and 0 < len(areas) <= 8192,
            'Select at least one reflective area.')
    grouped = {}
    used = set()
    for area in areas:
        require(isinstance(area, dict), 'The area selection is invalid.')
        surface = select_surface(doc, area.get('block'), mode='saved', faces=area.get('faces'),
                                 flip=area.get('flip', False), allow_disconnected=True)
        require(not doc.mesh(surface['block']).hidden, 'Hidden geometry cannot be added to the reflective selection.')
        key = surface['block'], surface['flip']
        faces = grouped.setdefault(key, set())
        for face in surface['faces']:
            identity = surface['block'], face
            require(identity not in used, 'The selection repeats a triangle.')
            used.add(identity)
            faces.add(face)
    require(len(used) <= 65535, 'One NIF pane supports at most 65,535 triangles.')
    regions = [select_surface(doc, block, mode='saved', faces=sorted(faces), flip=flip,
                              allow_disconnected=True) for (block, flip), faces in grouped.items()]
    points = [p for r in regions for face in r['faces'] for p in doc.mesh(r['block']).face(face)]
    extent = max(max(p[i] for p in points)-min(p[i] for p in points) for i in range(3))
    tolerance = max(1e-6, extent*1e-5)
    first = regions[0]
    normal = first['normal']
    require(all(dot(r['normal'], normal) >= math.cos(math.radians(.5)) for r in regions) and
            all(abs(dot(sub(p, first['origin']), normal)) <= tolerance for p in points),
            'These areas are not on the same flat plane and facing side. Angled or offset areas need separate reflections.')
    area = sum(r['area'] for r in regions)
    origin = mul(tuple(sum(r['origin'][i]*r['area'] for r in regions) for i in range(3)), 1/area)
    up = (0., 0., 1.) if abs(normal[2]) < .95 else (0., 1., 0.)
    right = unit(cross(up, normal)); up = unit(cross(normal, right))
    us = [dot(sub(p, origin), right) for p in points]
    vs = [dot(sub(p, origin), up) for p in points]
    return dict(areas=regions, triangleCount=len(used), area=area, origin=origin, normal=normal,
                right=right, up=up, width=max(us)-min(us), height=max(vs)-min(vs),
                boundary=[edge for r in regions for edge in r['boundary']])


def toggle_area(doc, current, clicked):
    if current is None:
        return clicked
    regions = combine_areas(doc, selection_areas(current))['areas']
    clicked_faces = set(clicked['faces'])
    selected_faces = {f for r in regions if r['block'] == clicked['block'] for f in r['faces']}
    if clicked_faces <= selected_faces:
        remaining = []
        for region in regions:
            faces = [f for f in region['faces'] if region['block'] != clicked['block'] or f not in clicked_faces]
            if faces:
                remaining.append(dict(block=region['block'], faces=faces, flip=region['flip']))
        return combine_areas(doc, remaining) if remaining else None
    # Adding an overlapping part of the same connected region must not repeat
    # triangles. Its existing facing remains authoritative for retained faces.
    faces = [f for f in clicked['faces'] if f not in selected_faces]
    return combine_areas(doc, regions+[dict(block=clicked['block'], faces=faces, flip=clicked['flip'])])


def export_areas(doc, mirror_type, selection):
    import mirror_creator_export as export
    combined = combine_areas(doc, selection_areas(selection))
    regions = combined['areas']
    model = nif.parse(doc.data)
    chosen = {}
    for region in regions:
        chosen.setdefault(region['block'], set()).update(region['faces'])
    parents = {}
    for i, block in enumerate(model.blocks):
        kind = model.type_name(i)
        if kind in ('NiNode', 'BSFadeNode'):
            for child in nif.parse_node(block).children:
                if child in chosen:
                    require(child not in parents, 'A selected part has more than one scene parent.')
                    parents[child] = i
        elif kind == 'BSTriShape':
            av = nif.parse_avobject(block)
            name = model.strings[av.name] if av.name != 0xffffffff else ''
            require(name != export.PANE_NAME or i in chosen,
                    'This NIF already has a prepared surface. Include it in the selection or use an unprepared model.')
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
            original = (original[0], original[2], original[1]) if region['flip'] else original
            tri = []
            for index in original:
                world = mesh.positions[index]
                local = mul(matvec(inverse, sub(world, translation)), 1/scale)
                # Validate/export the exact float32 vertices the game receives.
                local = struct.unpack('<3f', struct.pack('<3f', *local))
                if local not in indices:
                    indices[local] = len(positions); positions.append(local)
                tri.append(indices[local]); expected.append(world)
            triangles.append(tuple(tri))
    require(0 < len(positions) <= 65535, 'One NIF pane supports at most 65,535 vertices.')

    owned = {export.SCHEMA_NAME, export.PLANE_NAME, export.TRIANGLES_NAME, export.PROFILE_NAME}
    for index, faces in chosen.items():
        block = model.blocks[index]; av = nif.parse_avobject(block)
        retained = []
        for ref in av.extra_data:
            require(0 <= ref < len(model.blocks), 'A selected part has an invalid extra-data reference.')
            name = struct.unpack_from('<I', model.blocks[ref])[0]
            if name >= len(model.strings) or model.strings[name] not in owned:
                retained.append(ref)
        av.extra_data = retained
        if av.name != 0xffffffff and model.strings[av.name] == export.PANE_NAME:
            av.name = nif.string_index(model, f'MOSSourceSurface:{index}')
        remaining = [t for i, t in enumerate(doc.mesh(index).triangles) if i not in faces]
        if remaining:
            body = export._replace_triangles(block, remaining)[nif.parse_avobject(block).tail_offset:]
            model.blocks[index] = nif.serialize_avobject(av)+body
        else:
            # Retain the original AVObject identity, transform, extra data and
            # collision owner when all its render triangles move to the pane.
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
    av.name = nif.string_index(model, 'MOSCombinedAreaSource')
    av.extra_data = []; av.collision = -1; av.flags &= ~1
    # Stream-100 position/UV/normal/tangent layout used by the first-party pane.
    body = struct.pack('<4fiiiQHHI', *center, radius, -1, -1, -1,
                       0x1b00000650407, len(triangles), len(positions), len(geometry))
    pane = nif.append_block(model, 'BSTriShape', nif.serialize_avobject(av)+body+geometry+struct.pack('<I', 0))
    parent = nif.parse_node(model.blocks[parents[anchor.block]])
    parent.children.append(pane)
    model.blocks[parents[anchor.block]] = nif.serialize_node(parent)
    intermediate = load_nif(nif.serialize(model), doc.name)
    actual = [intermediate.mesh(pane).positions[v] for t in triangles for v in t]
    tolerance = max(1e-6, max(combined['width'], combined['height'])*1e-5)
    require(all(length(sub(a, b)) <= tolerance for a, b in zip(actual, expected)),
            'Combining these transforms loses surface precision. Simplify the model transforms and try again.')
    output, report = export.export_nif(intermediate, mirror_type,
                                      dict(block=pane, faces=list(range(len(triangles))), flip=False),
                                      _allow_disconnected=True)
    report.update(sourceBlocks=list(chosen), selectedAreas=len(regions), originalUnchanged=True)
    return output, report
