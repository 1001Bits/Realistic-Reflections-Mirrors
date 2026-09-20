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


