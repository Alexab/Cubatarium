"""Isolated own lobster anatomy proposal; never reads reference resources.

Retain all51 joints, parent relationships and angular keyframes. Refit the
eight knee/ankle bind positions and their translation keys together, preserving
the existing planted-foot Y compensation instead of overriding the new bind
with the old clip-local translations. Nothing is published here.
"""
import argparse
from bisect import bisect_right
from copy import deepcopy
import json
import math
from pathlib import Path
import shutil
import sys
import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from creaturegen.anatomy import build_model
from creaturegen.export_gltf import export_gltf
from creaturegen.pipeline import palette_for_recipe
from creaturegen.surface_materials import bake_surface_atlas
from creaturegen.texturegen import bake_icon, bake_flat
from build_creature_anatomy_pilots import sha


def segment(cube, a, b, width, height, child):
    """Positive-winding XZ shaft, dimension-refit atlas; exact endpoint centres."""
    dx, dz = b[0] - a[0], b[2] - a[2]
    length = math.hypot(dx, dz)
    px, pz = dz / length * width / 2, -dx / length * width / 2
    points = [[p[0] + sign * px, p[1], p[2] + sign * pz] for p in (a, b) for sign in (-1, 1)]
    origin = [math.floor(min(p[i] for p in points) - (height / 2 if i == 1 else 0)) for i in range(3)]
    end = [math.ceil(max(p[i] for p in points) + (height / 2 if i == 1 else 0)) for i in range(3)]
    size = [hi - lo for hi, lo in zip(end, origin)]
    cube['origin'], cube['size'] = origin, size
    cube['shape'] = {
        'planform': [[(p[0] - origin[0]) / size[0], (p[2] - origin[2]) / size[2]] for p in points],
        'planform_y': [[(p[1] + sign * height / 2 - origin[1]) / size[1] for p in points] for sign in (-1, 1)]}
    if b[0] < a[0]:
        interval = [max(p[0] for p in points[2:]), min(p[0] for p in points[:2])]
        weights = [1, 0]
    else:
        interval = [max(p[0] for p in points[:2]), min(p[0] for p in points[2:])]
        weights = [0, 1]
    assert interval[1] > interval[0]
    cube['blend'] = {'bone': child, 'axis': 'x', 'range': interval, 'weights': weights}


def proposal(recipe, variant='v1'):
    value = deepcopy(recipe)
    old = {b['name']: b for b in recipe['anatomy']['bones']}
    bones = {b['name']: b for b in value['anatomy']['bones']}
    carapace = bones['body']['cubes'][0]
    assert carapace['name'] == 'carapace' and carapace['size'] == [8, 5, 12]
    carapace['origin'][1], carapace['size'][1] = 2.5, 7
    for side, sign in [('left', -1), ('right', 1)]:
        for index in range(4):
            upper, lower, foot = (bones[f'{part}_{side}{index}'] for part in ('upper', 'lower', 'foot'))
            lower['pivot'][0], lower['pivot'][1] = sign * 8.5, 8.5
            foot['pivot'][0] = sign * 12.5
            segment(upper['cubes'][0], upper['pivot'], lower['pivot'], 1.5, 1.5, lower['name'])
            segment(lower['cubes'][0], lower['pivot'], foot['pivot'], 1.5, 1.5, foot['name'])
            foot['cubes'][0]['origin'][0] += sign * 4
            if variant in ('v2', 'v3'):
                foot['cubes'][0]['origin'][0] -= .5
                foot['cubes'][0]['size'][0] = 2
        if variant in ('v2', 'v3'):
            palm = bones[f'palm_{side}']['cubes'][0]
            assert palm['origin'][2] == -15 and palm['size'][2] == 4
            palm['origin'][2], palm['size'][2] = -18, 7
            bones[f'moving_finger_{side}']['pivot'][2] = -18
        for name in (f'palm_{side}', f'moving_finger_{side}'):
            for cube in bones[name]['cubes']:
                if 'finger' in cube['name']:
                    assert cube['origin'][2] == -19 and cube['size'][2] == 4
                    cube['origin'][2], cube['size'][2] = (-22, 7) if variant == 'v1' else (-23, 5)
    if variant == 'v3':
        value['palette_overrides']['belly'] = [85, 143, 177]
    rebased = []
    for clip_name, clip in value['motion']['clips'].items():
        for track in clip['tracks']:
            if track['path'] != 'translation': continue
            name = track['bone']; bone, previous = bones[name], old[name]
            parent = bone.get('parent')
            new_local = [bone['pivot'][i] - (bones[parent]['pivot'][i] if parent else 0) for i in range(3)]
            old_local = [previous['pivot'][i] - (old[parent]['pivot'][i] if parent else 0) for i in range(3)]
            if new_local == old_local: continue
            for time, vector in zip(track['times'], track['values']):
                for axis in range(3):
                    if axis == 1 and name.startswith('foot_'):
                        if variant == 'v3':
                            rotation = next((t for t in clip['tracks'] if t['bone'] == parent and t['path'] == 'rotation'), None)
                            angle = 0.
                            if rotation:
                                assert all(abs(v[1]) < 1e-8 and abs(v[2]) < 1e-8 for v in rotation['values'])
                                i = max(0, min(len(rotation['times']) - 2, bisect_right(rotation['times'], time) - 1))
                                a, b = rotation['times'][i:i + 2]
                                fraction = max(0., min(1., (time - a) / (b - a)))
                                angle = rotation['values'][i][0] * (1 - fraction) + rotation['values'][i + 1][0] * fraction
                            # Preserve the old lift trajectory; only compensate
                            # the changed knee-to-ankle vertical bind distance.
                            vector[axis] += (new_local[axis] - old_local[axis]) / (16 * math.cos(math.radians(angle)))
                        else:
                            vector[axis] *= new_local[axis] / old_local[axis]
                    else:
                        vector[axis] += (new_local[axis] - old_local[axis]) / 16
            rebased.append({'clip': clip_name, 'bone': name, 'old_bind_local_texels': old_local, 'new_bind_local_texels': new_local})
    return value, rebased


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qa-name', required=True)
    parser.add_argument('--source-root', type=Path, default=ROOT)
    parser.add_argument('--variant', choices=('v1', 'v2', 'v3'), default='v1')
    args = parser.parse_args()
    assert Path(args.qa_name).name == args.qa_name and args.qa_name not in ('.', '..')
    out = ROOT / 'assetgen_output/creatures' / args.qa_name
    assert not out.exists(), 'Fresh output required'
    models = out / 'models'; models.mkdir(parents=True)
    for kind in ('creatures', 'skins'):
        shutil.copytree(ROOT / 'models' / kind, models / kind, ignore=shutil.ignore_patterns('asset_manifest.json'))
    folder = models / 'creatures/lobster'
    if args.source_root.resolve() != ROOT:
        shutil.copytree(args.source_root / 'models/creatures/lobster', folder, dirs_exist_ok=True)
    source = args.source_root / 'tools/creature_recipes/lobster.yaml'
    recipe = yaml.load(source.read_text(encoding='utf-8'), Loader=yaml.CSafeLoader)
    definition = json.loads((folder / 'creature.json').read_text())
    published = json.loads((folder / 'model.gltf').read_text())
    clips = [a['name'] for a in published['animations']]
    before, blob = export_gltf(build_model(recipe), clips)
    assert before == published and blob == (folder / 'model.bin').read_bytes()
    changed, rebased = proposal(recipe, args.variant)
    model = build_model(changed); after, data = export_gltf(model, clips)
    assert [b['name'] for b in recipe['anatomy']['bones']] == [b['name'] for b in changed['anatomy']['bones']]
    assert [b.get('parent') for b in recipe['anatomy']['bones']] == [b.get('parent') for b in changed['anatomy']['bones']]
    assert before['skins'] == after['skins']
    palettes = yaml.load((ROOT / 'tools/creature_palettes.yaml').read_text(encoding='utf-8'), Loader=yaml.CSafeLoader)['palettes']
    palette = palette_for_recipe(changed, palettes)
    atlas = bake_surface_atlas(model, palette, changed['surface_style'])
    for material in after['materials']:
        name = material['name']
        image = atlas if name == 'body' else bake_flat(palette, name, changed['pattern'], 'lobster', atlas.size)
        image.save(folder / 'textures' / (name + '.png'))
    bake_icon(atlas).save(folder / 'textures/icon.png')
    (folder / 'model.gltf').write_text(json.dumps(after, indent=2) + '\n', encoding='utf-8')
    (folder / 'model.bin').write_bytes(data)
    (out / 'recipes').mkdir()
    (out / 'recipes/lobster.yaml').write_text(yaml.safe_dump(changed, sort_keys=False, allow_unicode=True), encoding='utf-8')
    assert json.loads((folder / 'creature.json').read_text()) == definition
    report = {'schema': 'lobster-anatomy-proposal.v1', 'state': 'unapproved', 'variant': args.variant, 'builder_sha256': sha(Path(__file__)),
              'source_recipe_sha256': sha(source), 'source_exact_to_own_payload': True,
              'scope': 'Own blue lobster: fuller carapace, wider raised leg knees and longer claws;51 joints/hierarchy and angular keys retained.16 leg bind positions (plus two finger hinges in v2/v3) and dependent translation keys deliberately refit. V3 preserves the old foot lift rather than scaling it, and rebakes a steel-blue ventral palette. Not a byte-identical rig claim.',
              'rebased_translation_tracks': rebased, 'payload_sha256': {str(p.relative_to(folder)): sha(p) for p in folder.rglob('*') if p.is_file()}}
    (out / 'proposal_report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('UNAPPROVED', out, 'translation tracks rebased', len(rebased))


if __name__ == '__main__': main()
