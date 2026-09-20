#!/usr/bin/env python3
"""Local Fallout 4 NIF surface picker and portable selection projects.

The loaded source is retained byte-for-byte; export produces a separate NIF.
"""
from __future__ import annotations

import hashlib
import io
import json
import math
import struct
import zipfile
from collections import defaultdict, deque
from dataclasses import dataclass, field
from pathlib import PureWindowsPath

import nif_block_edit as nif_edit

MAX_NIF_BYTES = 64 * 1024 * 1024
MAX_PROJECT_BYTES = MAX_NIF_BYTES + 8 * 1024 * 1024
MAX_BLOCKS = 4096
MAX_VERTICES = 250_000
MAX_TRIANGLES = 500_000
PROJECT_FORMAT = "mirrors-of-fallout/surface-selection"
IDENTITY = ((1., 0., 0.), (0., 1., 0.), (0., 0., 1.))


class CreatorError(ValueError):
    """An understandable import or selection failure, safe to display."""


def require(condition, message):
    if not condition:
        raise CreatorError(message)


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def mul(a, s):
    return tuple(x * s for x in a)


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def length(a):
    return math.sqrt(dot(a, a))


def unit(a):
    n = length(a)
    require(n > 1e-12, "The selected surface contains a collapsed triangle.")
    return mul(a, 1/n)


def matvec(m, v):
    return tuple(dot(row, v) for row in m)


def matmul(a, b):
    return tuple(tuple(dot(row, col) for col in zip(*b)) for row in a)


def filename(value):
    name = PureWindowsPath(str(value)).name
    name = "".join(c for c in name if ord(c) >= 32 and c not in '<>:"/\\|?*')[:180]
    return name if name.lower().endswith('.nif') else "mirror.nif"


@dataclass
class Mesh:
    block: int
    name: str
    positions: list[tuple[float, float, float]]
    triangles: list[tuple[int, int, int]]
    hidden: bool = False
    _topology: object = field(default=None, repr=False)
    transform: tuple = ((0., 0., 0.), IDENTITY, 1.)
    ck_index: int | None = None
    ck_name: str = ''

    @property
    def extent(self):
        return max(max(v[i] for v in self.positions)-min(v[i] for v in self.positions) for i in range(3))

    def face(self, index):
        return [self.positions[v] for v in self.triangles[index]]

    def topology(self):
        """Weld only positional seams for connectivity; never alter source data."""
        if self._topology is not None:
            return self._topology
        tolerance = max(1e-7, self.extent * 1e-7)
        # Exact positions plus small quantization accommodate duplicated UV seams.
        keys = [tuple(round(c/tolerance) for c in p) for p in self.positions]
        welded = {}; representatives = {}; vertex_keys = []
        for i, key in enumerate(keys):
            if key not in welded:
                welded[key] = i
                representatives[i] = self.positions[i]
            vertex_keys.append(welded[key])
        edges = defaultdict(list)
        face_edges = []
        for index, tri in enumerate(self.triangles):
            w = [vertex_keys[v] for v in tri]
            pairs = [(w[0], w[1]), (w[1], w[2]), (w[2], w[0])]
            face_edges.append(pairs)
            for a, b in pairs:
                edges[tuple(sorted((a, b)))].append(index)
        self._topology = (edges, face_edges, representatives)
        return self._topology

    def public(self):
        return dict(block=self.block, name=self.name, hidden=self.hidden,
                    ckName=self.ck_name, ckIndex=self.ck_index,
                    positions=[v for p in self.positions for v in p],
                    indices=[v for t in self.triangles for v in t],
                    vertexCount=len(self.positions), triangleCount=len(self.triangles))


@dataclass
class Document:
    data: bytes
    name: str
    meshes: list[Mesh]
    warnings: list[str]

    @property
    def digest(self):
        return hashlib.sha256(self.data).hexdigest()

    def mesh(self, block):
        require(type(block) is int, "Choose a part in the loaded model.")
        result = next((m for m in self.meshes if m.block == block), None)
        require(result is not None, "That part does not belong to this NIF.")
        return result

    def public(self):
        return dict(id=self.digest, name=self.name, bytes=len(self.data),
                    profile="Fallout 4 · static BSTriShape", warnings=self.warnings,
                    meshes=[m.public() for m in self.meshes])


def load_nif(data: bytes, name="mirror.nif") -> Document:
    require(0 < len(data) <= MAX_NIF_BYTES, "Choose a NIF smaller than 64 MB.")
    try:
        newline = data.index(b'\n') + 1
        require(newline < 128, "This file does not have a supported NIF header.")
        version, endian, user, count, stream = struct.unpack_from('<IBIII', data, newline)
        require(version == 0x14020007 and endian == 1 and user == 12 and stream == 130,
                "Choose a Fallout 4 NIF (stream 130). Skyrim and other-game NIFs are not supported.")
        require(0 < count <= MAX_BLOCKS, "This preview supports up to 4,096 NIF blocks.")
        nif = nif_edit.parse(data)
        require(all(0 <= t < len(nif.types) for t in nif.block_types), "The NIF has an invalid block type reference.")
        roots = struct.unpack_from(f'<{len(nif.footer)//4-1}i', nif.footer, 4)
        require(bool(roots), "The NIF has no scene root.")
        meshes = []; visited = set(); warnings = []

        def walk(index, parent_t=(0., 0., 0.), parent_r=IDENTITY, parent_s=1., hidden=False, depth=0):
            if index == -1:
                return
            require(0 <= index < len(nif.blocks), "The NIF has an invalid scene reference.")
            require(index not in visited, "This preview does not support cyclic or instanced scene nodes.")
            require(depth < 96, "The NIF scene hierarchy is too deep for this preview.")
            visited.add(index)
            kind = nif.type_name(index); block = nif.blocks[index]
            require(kind in ('NiNode', 'BSFadeNode', 'BSTriShape'),
                    f"This preview cannot read scene part {kind}. Use a static Fallout 4 NIF with BSTriShape geometry.")
            av = nif_edit.parse_avobject(block)
            require(av.controller == -1, "Animated NIF controllers are not supported in this static preview.")
            values = struct.unpack('<13f', av.transform)
            require(all(math.isfinite(v) for v in values), "The NIF contains a non-finite transform.")
            # NIF Matrix33 is stored column-major; matvec uses row vectors.
            t = values[:3]; r = tuple(tuple(values[3+j*3+i] for j in range(3)) for i in range(3)); s = values[12]
            require(1e-5 < s <= 1e7, "The NIF contains a negative, collapsed or excessive scale.")
            require(all(abs(dot(r[i], r[j])-(1 if i == j else 0)) < .005 for i in range(3) for j in range(3)),
                    "This preview does not support sheared NIF transforms.")
            require(dot(cross(r[0], r[1]), r[2]) > .995, "Apply mirrored transforms in your model editor before importing.")
            world_t = add(parent_t, mul(matvec(parent_r, t), parent_s))
            world_r = matmul(parent_r, r); world_s = parent_s*s
            hidden = hidden or bool(av.flags & 1)
            if kind != 'BSTriShape':
                node = nif_edit.parse_node(block)
                for child in node.children:
                    walk(child, world_t, world_r, world_s, hidden, depth+1)
                return
            name_index = av.name
            require(name_index == 0xffffffff or name_index < len(nif.strings), "The NIF has an invalid part name.")
            part_name = nif.strings[name_index] if name_index != 0xffffffff else ''
            off = av.tail_offset
            skin = struct.unpack_from('<i', block, off+16)[0]
            desc, nt, nv, size = struct.unpack_from('<QIHI', block, off+28)
            stride = (desc & 15)*4; flags = desc >> 44
            require(skin == -1 and not flags & 0x1c0, f'“{part_name}” is skinned or has unsupported dynamic vertex data.')
            position_format = '<3f' if flags & 0x400 else '<3e'
            require(flags & 1 and (16 if flags & 0x400 else 8) <= stride <= 60 and not (desc & 0xf0), f'“{part_name}” has unsupported or missing vertex positions.')
            require(nv > 0 and nt > 0, f'“{part_name}” contains no triangles.')
            require(size == nv*stride + nt*6 and len(block) == off+46+size,
                    f'“{part_name}” has inconsistent vertex or triangle data.')
            shader = struct.unpack_from('<i', block, off+20)[0]
            require(0 <= shader < len(nif.blocks), f'“{part_name}” has no valid shader.')
            kind = nif.type_name(shader)
            require(kind in ('BSLightingShaderProperty', 'BSEffectShaderProperty'), f'“{part_name}” uses an unsupported shader.')
            prop = nif.blocks[shader]; prop_offset = 4 if kind == 'BSLightingShaderProperty' else 0
            _, extra_count = struct.unpack_from('<II', prop, prop_offset)
            require(extra_count <= 64 and struct.unpack_from('<i', prop, prop_offset+8+extra_count*4)[0] == -1,
                    f'“{part_name}” has an animated shader.')
            pos = []
            for v in range(nv):
                local = struct.unpack_from(position_format, block, off+46+v*stride)
                world = add(world_t, mul(matvec(world_r, local), world_s))
                require(all(math.isfinite(c) and abs(c) <= 1e7 for c in world), f'“{part_name}” contains invalid or excessive coordinates.')
                pos.append(world)
            triangles = list(struct.iter_unpack('<3H', block[off+46+nv*stride:off+46+size]))
            require(all(max(tri) < nv for tri in triangles), f'“{part_name}” has a triangle with a missing vertex.')
            require(sum(len(m.positions) for m in meshes)+nv <= MAX_VERTICES and
                    sum(len(m.triangles) for m in meshes)+nt <= MAX_TRIANGLES,
                    'This preview supports at most 250,000 vertices and 500,000 triangles.')
            # The Fallout CK helper identifies parts by their name and NIF block.
            meshes.append(Mesh(index, part_name or f'Part {index}', pos, triangles, hidden,
                               transform=(world_t, world_r, world_s),
                               ck_index=len(meshes), ck_name=part_name))

        for root in roots:
            walk(root)
        require(bool(meshes), "No supported mesh was found in this NIF.")
        if any(m.hidden for m in meshes):
            warnings.append('Some parts are hidden in the NIF and cannot be selected.')
        warnings.append('Geometry preview: game textures, animations and collision are not displayed.')
        return Document(data, filename(name), meshes, warnings)
    except CreatorError:
        raise
    except (ValueError, IndexError, struct.error, OverflowError) as exc:
        raise CreatorError(f"The NIF could not be read: {exc}") from exc


def select_surface(doc: Document, block: int, seed: int | None = None,
                   mode='connected', faces: list[int] | None = None, flip=False,
                   allow_disconnected=False):
    mesh = doc.mesh(block)
    require(type(flip) is bool, "Facing must be either front or reversed.")
    require(mode in ('connected', 'part', 'saved'), "Choose flat region or whole part.")
    if mode == 'part':
        selected = list(range(len(mesh.triangles)))
    elif mode == 'saved':
        require(isinstance(faces, list) and 0 < len(faces) <= len(mesh.triangles), "The saved surface has an invalid face selection.")
        require(all(type(i) is int and 0 <= i < len(mesh.triangles) for i in faces), "The saved surface references a missing triangle.")
        require(len(set(faces)) == len(faces), "The saved surface repeats a triangle.")
        selected = sorted(faces)
    else:
        require(type(seed) is int and 0 <= seed < len(mesh.triangles), "Click a triangle on the intended surface.")
        selected = [seed]
    a, b, c = mesh.face(selected[0]); normal = unit(cross(sub(b, a), sub(c, a)))
    tolerance = max(1e-6, mesh.extent*1e-5)

    def coplanar(index):
        p, q, r = mesh.face(index)
        n = cross(sub(q, p), sub(r, p)); magnitude = length(n)
        return (magnitude > 1e-12 and dot(mul(n, 1/magnitude), normal) >= math.cos(math.radians(.5)) and
                all(abs(dot(sub(v, a), normal)) <= tolerance for v in (p, q, r)))

    edges, face_edges, representatives = mesh.topology()
    if mode == 'connected':
        seen = {seed}; todo = deque([seed])
        while todo:
            for x, y in face_edges[todo.popleft()]:
                for neighbor in edges[tuple(sorted((x, y)))]:
                    if neighbor not in seen and coplanar(neighbor):
                        seen.add(neighbor); todo.append(neighbor)
        selected = sorted(seen)
    require(all(coplanar(i) for i in selected),
            'This selection is not one flat, consistently facing surface. Angled or offset areas need separate reflections.')
    chosen = set(selected); connected = {selected[0]}; todo = deque(connected)
    while todo:
        for x, y in face_edges[todo.popleft()]:
            for neighbor in edges[tuple(sorted((x, y)))]:
                if neighbor in chosen and neighbor not in connected:
                    connected.add(neighbor); todo.append(neighbor)
    require(allow_disconnected or connected == chosen,
            'The selection contains disconnected surfaces. Select one connected flat region.')
    counts = defaultdict(int); directed = {}
    area = 0.; centroid = (0., 0., 0.)
    for index in selected:
        p, q, r = mesh.face(index)
        triangle_area = length(cross(sub(q, p), sub(r, p)))*.5
        area += triangle_area
        centroid = add(centroid, mul(add(add(p, q), r), triangle_area/3))
        for x, y in face_edges[index]:
            edge = tuple(sorted((x, y))); counts[edge] += 1; directed[edge] = (x, y)
    require(all(n <= 2 for n in counts.values()), 'This surface has overlapping or non-manifold edges. Repair it before selecting it.')
    boundary = [directed[e] for e, count in counts.items() if count == 1]
    require(bool(boundary), 'The selected geometry has no open pane outline.')
    degrees = defaultdict(int)
    for x, y in boundary:
        degrees[x] += 1; degrees[y] += 1
    require(all(n >= 2 and n % 2 == 0 if allow_disconnected else n == 2 for n in degrees.values()),
            'The selected outline branches or touches itself. Repair it before selecting it.')
    normal = mul(normal, -1 if flip else 1)
    origin = mul(centroid, 1/area)
    up = (0., 0., 1.) if abs(normal[2]) < .95 else (0., 1., 0.)
    right = unit(cross(up, normal)); up = unit(cross(normal, right))
    points = [mesh.positions[v] for i in selected for v in mesh.triangles[i]]
    us = [dot(sub(p, origin), right) for p in points]; vs = [dot(sub(p, origin), up) for p in points]
    return dict(block=block, name=mesh.name, faces=selected, flip=flip,
                triangleCount=len(selected), area=area, origin=origin, normal=normal,
                right=right, up=up, width=max(us)-min(us), height=max(vs)-min(vs),
                boundary=[[representatives[x], representatives[y]] for x, y in boundary])


def save_project(doc: Document, mirror_type: str, selection: dict) -> bytes:
    require(mirror_type == 'placeable', 'Fallout Mirror Creator supports placed static mirrors.')
    require(isinstance(selection, dict), 'Select the reflective surface before saving.')
    surface = select_surface(doc, selection.get('block'), mode='saved',
                             faces=selection.get('faces'), flip=selection.get('flip', False))
    project = dict(format=PROJECT_FORMAT, version=1, mirrorType=mirror_type,
                   asset=dict(file='asset.nif', originalName=doc.name, sha256=doc.digest),
                   selection=dict(block=surface['block'], faces=surface['faces'], flip=surface['flip']),
                   playableAddon=False)
    out = io.BytesIO()
    with zipfile.ZipFile(out, 'w', compression=zipfile.ZIP_STORED) as archive:
        entries = {'asset.nif': doc.data,
                   'project.json': (json.dumps(project, indent=2, sort_keys=True)+'\n').encode(),
                   'README.txt': b'Mirror Creator surface-selection project.\nOpen this .mirror-project file in Mirror Creator to resume.\nasset.nif is the unchanged original; no playable add-on is generated.\n'}
        for name, data in entries.items():
            info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            info.external_attr = 0o644 << 16
            archive.writestr(info, data)
    return out.getvalue()


def load_project(data: bytes):
    require(0 < len(data) <= MAX_PROJECT_BYTES, 'The project is too large for this preview.')
    try:
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            names = archive.namelist()
            require(len(names) == 3 and set(names) == {'asset.nif', 'project.json', 'README.txt'}, 'This is not a Mirror Creator selection project.')
            for info in archive.infolist():
                limit = MAX_NIF_BYTES if info.filename == 'asset.nif' else 8*1024*1024
                require(info.file_size <= limit and info.compress_type == zipfile.ZIP_STORED and not info.flag_bits & 1,
                        'The project contains oversized, compressed or encrypted entries.')
            project = json.loads(archive.read('project.json'))
            require(isinstance(project, dict) and project.get('format') == PROJECT_FORMAT and type(project.get('version')) is int and project['version'] == 1,
                    'This project format or version is not supported.')
            require(project.get('mirrorType') == 'placeable' and project.get('playableAddon') is False, 'The project has an unsupported mirror profile.')
            asset = project.get('asset'); selection = project.get('selection')
            require(isinstance(asset, dict) and asset.get('file') == 'asset.nif' and isinstance(selection, dict), 'The project is missing its model or surface.')
            raw = archive.read('asset.nif')
            require(hashlib.sha256(raw).hexdigest() == asset.get('sha256'), 'The NIF has changed since this selection was saved. Load the NIF again and reselect its surface.')
            doc = load_nif(raw, asset.get('originalName', 'mirror.nif'))
            surface = select_surface(doc, selection.get('block'), mode='saved', faces=selection.get('faces'), flip=selection.get('flip', False))
            return doc, project['mirrorType'], surface
    except CreatorError:
        raise
    except (ValueError, KeyError, TypeError, zipfile.BadZipFile, RuntimeError) as exc:
        raise CreatorError(f'The selection project could not be read: {exc}') from exc
