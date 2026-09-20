"""Build a deterministic, non-localized FO4/VR mirror ESM with Fallout4.esm as its only master.

Uses existing vanilla decoration menu keywords and placement patterns. No vanilla overrides,
Papyrus scripts, injected form lists, DLC, ESL support or menu-manager dependency are needed.
"""
import argparse
import json
import math
from pathlib import Path
import struct

CATEGORY_ID = 0x01000A00
PLUGIN = 'Realistic Reflections - Mirrors.esm'
SURFACE_ID = 0x01000B00
SWAP_ID = 0x01000B01
CATEGORY_ART_ID = 0x01000B02
CATEGORY_PREVIEW_MODEL = 'CabinetWallMedium'
SIZE_ORDER = ('Small', 'Medium', 'Large', 'Very Large', 'Gigantic')
# Vanilla cracked wall mirrors (Props\Mirrors) in the order they appear in the workshop row, after the fixtures.
CRACKED_ORDER = ('CrackedMirror01', 'CrackedMirror01a', 'CrackedMirror02', 'CrackedMirror02a',
                 'CrackedMirror03', 'CrackedMirror03a')
DESIGN_NAMES = {'Cabinet': 'Classic', 'Round': 'Round', 'Vanity': 'Vanity',
                'CabinetUnit': 'Bathroom Cupboard', 'RoundMetal': 'Round Metal', 'Cracked': 'Cracked'}
MATERIAL = r'MirrorsOfFallout\Authoring\MOF_MirrorSurface.bgsm'
TEXTURES = 'MirrorsOfFallout\\Authoring\\'
SOURCE = r'SetDressing\PlayerHouse\PlayerHouse_BathroomMirrorENV01.BGSM'

def field(signature, data):
    return signature.encode('ascii')+struct.pack('<H', len(data))+data


def string(signature, text):
    return field(signature, text.encode('utf-8')+b'\0')


def form(signature, form_id):
    return field(signature, struct.pack('<I', form_id))


def record(signature, form_id, fields):
    body = b''.join(fields)
    return struct.pack('<4sIIIIHH', signature.encode('ascii'), len(body), 0, form_id, 0, 131, 0)+body


def group(signature, records):
    body = b''.join(records)
    return struct.pack('<4sI4sIHHI', b'GRUP', len(body)+24, signature.encode('ascii'), 0, 0, 0, 0)+body


def workshop_order(model):
    """Size pairs first, then the three complete fixtures; never reorder IDs."""
    if model['design'] in ('Cabinet', 'Round'):
        return SIZE_ORDER.index(model.get('size', 'Small')), model['design'] == 'Round'
    if model['design'] == 'Cracked':
        return len(SIZE_ORDER)+1, CRACKED_ORDER.index(model['name'])
    return len(SIZE_ORDER), ('Vanity', 'CabinetUnit', 'RoundMetal').index(model['design'])


def build_plugin(manifest):
    """Workshop and authoring share one ESM; all existing local IDs stay fixed."""
    if manifest['plugin'] != PLUGIN:
        raise ValueError('The workshop catalogue must use the shared plugin name')
    ordered = sorted((model for model in manifest['models'] if model.get('buildable', True)), key=workshop_order)
    priorities = {model['name']: 10*(index+1) for index, model in enumerate(ordered)}
    preview = next(model for model in manifest['models'] if model['name'] == CATEGORY_PREVIEW_MODEL)
    statics, recipes = [], []
    for i, model in enumerate(manifest['models']):
        form_id = 0x01000000 | model['form_id']
        wall = model['placement'] == 'Wall'
        design = DESIGN_NAMES[model['design']]
        display = f'{design} {model["placement"]} Mirror'
        if model.get('size'):
            display += ' - '+model['size']
        lo, hi = model['bounds']
        obnd = struct.pack('<6h', *(math.floor(v) for v in lo), *(math.ceil(v) for v in hi))
        statics.append(record('STAT', form_id, [
            string('EDID', 'MOF_Workshop_'+model['name']), field('OBND', obnd),
            form('PTRN', 0x000B102F if wall else 0x001B4ACE),
            string('MODL', model['model']), string('FULL', display),
            field('DNAM', struct.pack('<fIff', 90., 0, 1., 1.))]))
        if not model.get('buildable', True):
            continue  # Retain the base FormID for existing saves, remove the workshop recipe.
        costs = {'Cabinet': (4, 4, 2), 'Round': (2, 2, 1), 'Vanity': (3, 3, 1),
                 'CabinetUnit': (4, 8, 3), 'RoundMetal': (2, 4, 2), 'Cracked': (2, 1, 1)}[model['design']]
        # Larger panes use more material; the existing Small recipes are unchanged.
        glass, steel, screws = (math.ceil(cost*model.get('scale', 1.)**2) for cost in costs)
        if not wall:
            steel += 3
        recipes.append(record('COBJ', 0x01000900+i, [
            string('EDID', 'MOF_Workshop_co_'+model['name']),
            form('YNAM', 0x000196B0 if wall else 0x0002B014),
            form('ZNAM', 0x000196BA if wall else 0x0002B015),
            field('FVPA', struct.pack('<6I', 0x0001FAA4, glass, 0x0001FABD, steel, 0x0003D294, screws)),
            string('DESC', ''), form('CNAM', form_id),
            form('BNAM', 0x0008280B if wall else 0x0005A0C8),
            # The category inherits its first recipe's preview. A menu-art
            # override keeps that preview medium while CNAM still builds Small.
            *([form('ANAM', CATEGORY_ART_ID)] if model is ordered[0] else []),
            form('FNAM', CATEGORY_ID),
            field('INTV', struct.pack('<HH', 1, priorities[model['name']]))]))
    category = record('KYWD', CATEGORY_ID, [
        string('EDID', 'MOF_WorkshopRecipeFilterMirrors'),
        field('CNAM', struct.pack('<I', 0x00FFFFFF)),
        field('TNAM', struct.pack('<I', 9)), string('FULL', 'Mirrors')])
    surface = record('TXST', SURFACE_ID, [string('EDID', 'MOF_MirrorSurface'), field('OBND', bytes(12)),
        string('TX00', TEXTURES+'mirror_surface.dds'), string('TX01', TEXTURES+'mirror_surface_n.dds'),
        string('TX07', TEXTURES+'mirror_surface_s.dds'), field('DNAM', bytes(2)), string('MNAM', MATERIAL)])
    swap = record('MSWP', SWAP_ID, [string('EDID', 'MOF_MirrorSurfaceSwapTemplate'),
        string('BNAM', SOURCE), string('SNAM', MATERIAL)])
    lo, hi = preview['bounds']
    category_art = record('ARTO', CATEGORY_ART_ID, [string('EDID', 'MOF_WorkshopCategoryPreview'),
        field('OBND', struct.pack('<6h', *(math.floor(v) for v in lo), *(math.ceil(v) for v in hi))),
        string('MODL', preview['model']), field('DNAM', struct.pack('<I', 0))])
    # CK includes the six top-level GRUP records in HEDR's record count.
    header = bytearray(record('TES4', 0, [field('HEDR', struct.pack('<fII', 1., 10+len(statics)+len(recipes), 0xB03)),
        string('CNAM', 'Realistic Reflections - Mirrors'),
        string('SNAM', 'Realistic Reflections - Mirrors 1.0. Workshop mirrors and MOF_MirrorSurface authoring material. Retains retired standing forms for existing saves.'),
        string('MAST', 'Fallout4.esm'), field('DATA', b'\0'*8)]))
    # The ESM is a full master (not ESL). Preserve all local FormIDs and let
    # native CK retain it as an author plugin's dependency on save.
    struct.pack_into('<I', header, 8, 1)
    return (bytes(header)+group('KYWD', [category])+group('TXST', [surface])+group('MSWP', [swap])+group('ARTO', [category_art])+
            group('STAT', statics)+group('COBJ', recipes))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding='utf-8'))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(build_plugin(manifest))
    print(f'Built {args.output.name}: workshop mirrors and MOF_MirrorSurface; no vanilla overrides.')


if __name__ == '__main__':
    main()
