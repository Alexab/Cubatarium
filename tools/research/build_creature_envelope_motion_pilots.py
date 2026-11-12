"""Build isolated authored motion proposals from existing own recipes.

No reference asset is read by this generator invocation. Canonical recipes,
models and bin/models are unchanged; this is an unapproved QA proposal catalog.
"""
from copy import deepcopy
import argparse
import hashlib
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


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def add_roll(recipe, clip_name, amplitude):
    tracks = recipe['motion']['clips'][clip_name]['tracks']
    duration = max(t['times'][-1] for t in tracks)
    assert not any(t['bone'] == 'body' and t['path'] == 'rotation' for t in tracks)
    times = [round(duration * i / 32, 8) for i in range(33)]
    tracks.append({'bone': 'body', 'path': 'rotation', 'times': times,
                   'values': [[0., 0., round(amplitude * math.sin(2 * math.pi * i / 32), 6)] for i in range(33)]})
    recipe['motion']['clips'][clip_name]['contact'].update(geometry='skinned_mesh', samples=257)


def thicken_branch(cube, width=2.15, height=1.6):
    """Thicken each shaft section around its own centre, keeping its centreline.

    Refit integer texel dimensions/UV allocation to the larger branch instead
    of stretching the old texture over an enlarged planform.
    """
    shape = cube['shape']
    origin, size = cube['origin'][:], cube['size'][:]
    points = [[origin[0] + size[0] * x, origin[2] + size[2] * z] for x, z in shape['planform']]
    for pair in ((0, 1), (2, 3)):
        centre = [(points[pair[0]][a] + points[pair[1]][a]) / 2 for a in range(2)]
        for i in pair:
            points[i] = [centre[a] + (points[i][a] - centre[a]) * width for a in range(2)]
    low, high = shape['planform_y']
    lower, upper = [], []
    for a, b in zip(low, high):
        centre = origin[1] + size[1] * (a + b) / 2
        half = size[1] * (b - a) * height / 2
        lower.append(centre - half)
        upper.append(centre + half)
    new_origin = [math.floor(min(p[0] for p in points)), math.floor(min(lower)), math.floor(min(p[1] for p in points))]
    new_end = [math.ceil(max(p[0] for p in points)), math.ceil(max(upper)), math.ceil(max(p[1] for p in points))]
    new_size = [b - a for a, b in zip(new_origin, new_end)]
    shape['planform'] = [[(x - new_origin[0]) / new_size[0], (z - new_origin[2]) / new_size[2]] for x, z in points]
    shape['planform_y'] = [[(y - new_origin[1]) / new_size[1] for y in values] for values in (lower, upper)]
    cube['origin'], cube['size'] = new_origin, new_size


def proposal(sid, recipe, variant):
    value = deepcopy(recipe)
    if sid == 'badger':
        add_roll(value, 'walk', 6.)
        add_roll(value, 'run', 9.)
    elif sid == 'treeman':
        add_roll(value, 'walk', 22.)
        add_roll(value, 'run', 26.)
        for clip in value['motion']['clips'].values():
            clip['contact'].update(geometry='skinned_mesh', samples=257)
        for bone in value['anatomy']['bones']:
            if variant == 'v5':
                for cube in bone.get('cubes', []):
                    if cube['name'] in ('trunk_base', 'trunk_upper'):
                        centre = cube['origin'][0] + cube['size'][0] / 2
                        cube['size'][0] = 14
                        cube['origin'][0] = centre - 7
                    if bone['name'].startswith(('upper_arm_', 'forearm_', 'thigh_', 'shin_')) and 'planform_y' in cube.get('shape', {}):
                        thicken_branch(cube)
                continue
            if not bone['name'].startswith(('upper_arm_', 'forearm_', 'thigh_', 'shin_')):
                continue
            for cube in bone.get('cubes', []):
                shape = cube.get('shape', {})
                if 'planform' in shape:
                    shape['planform'] = [[.5 + (x - .5) * 1.45, .5 + (z - .5) * 1.2]
                                         for x, z in shape['planform']]
    elif sid == 'lobster':
        for track in value['motion']['clips']['punch']['tracks']:
            if track['bone'].startswith('claw_upper_') and track['path'] == 'rotation':
                duration = track['times'][-1]
                for time, vector in zip(track['times'], track['values']):
                    vector[0] = round(55 * math.sin(math.pi * time / duration) ** 2, 6)
            elif track['bone'].startswith('forearm_') and track['path'] == 'rotation':
                duration = track['times'][-1]
                for time, vector in zip(track['times'], track['values']):
                    vector[0] += round(15 * math.sin(math.pi * time / duration) ** 2, 6)
        if variant in ('v4', 'v5'):
            clip = value['motion']['clips']['punch']
            duration = max(t['times'][-1] for t in clip['tracks'])
            assert not any(t['bone'] == 'body' and t['path'] == 'rotation' for t in clip['tracks'])
            times = [round(duration * i / 32, 8) for i in range(33)]
            clip['tracks'].append({'bone': 'body', 'path': 'rotation', 'times': times,
                                    'values': [[round(15 * math.sin(math.pi * i / 32) ** 2, 6), 0., 0.] for i in range(33)]})
            clip['contact'].update(geometry='skinned_mesh', samples=257)
    elif sid == 'seahorse':
        bones = {bone['name']: bone for bone in value['anatomy']['bones']}
        corrections = {}
        previous_angle = 0.
        for index in range(6):
            a, b = bones[f'tail{index}']['pivot'], bones[f'tail{index + 1}']['pivot']
            angle = math.degrees(math.atan2(b[2] - a[2], -(b[1] - a[1])))
            while angle < previous_angle - 180:
                angle += 360
            while angle > previous_angle + 180:
                angle -= 360
            corrections[f'tail{index}'] = angle - previous_angle
            previous_angle = angle
        for track in value['motion']['clips']['fly']['tracks']:
            if track['bone'].startswith('tail') and track['path'] == 'rotation':
                duration = track['times'][-1]
                for time, vector in zip(track['times'], track['values']):
                    if variant == 'v3':
                        vector[0] = round(vector[0] * 4, 6)
                    else:
                        extension = .65 * math.sin(math.pi * time / duration) ** 2
                        vector[0] = round(vector[0] + corrections.get(track['bone'], 0.) * extension, 6)
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--variant', choices=('v3', 'v4', 'v5'), default='v4')
    parser.add_argument('--qa-name')
    parser.add_argument('--source-root', type=Path, default=ROOT, help='Project-shaped own pre-publish snapshot for reproducible historical proposals')
    args = parser.parse_args()
    name = args.qa_name or ('envelope_motion_' + args.variant)
    assert Path(name).name == name and name not in ('.', '..')
    qa = ROOT / 'assetgen_output/creatures' / name
    assert not qa.exists(), 'Proposal output must be fresh, never overwrite accepted or previous evidence'
    models = qa / 'models'
    models.mkdir(parents=True)
    for kind in ('creatures', 'skins'):
        shutil.copytree(ROOT / 'models' / kind, models / kind,
                        ignore=shutil.ignore_patterns('asset_manifest.json'))
    recipes = qa / 'recipes'
    recipes.mkdir()
    report = {'schema': 'creature-envelope-motion-proposals.v1', 'state': 'unapproved_proposals',
              'variant': args.variant, 'builder_sha256': sha(Path(__file__)),
              'scope': 'Own recipe edits only; rig nodes/skin and definitions/colliders unchanged; v5 treeman refits UV/texel dimensions and rebakes own texture; not published or art accepted',
              'species': []}
    for sid in ('badger', 'treeman', 'lobster', 'seahorse'):
        source_recipe = args.source_root / 'tools/creature_recipes' / (sid + '.yaml')
        recipe = yaml.load(source_recipe.read_text(encoding='utf8'), Loader=yaml.CSafeLoader)
        folder = models / 'creatures' / sid
        if args.source_root.resolve() != ROOT:
            shutil.copytree(args.source_root / 'models/creatures' / sid, folder, dirs_exist_ok=True)
        definition = json.loads((folder / 'creature.json').read_text())
        clips = list(dict.fromkeys(definition['visual']['animation']['state_map'].values()))
        baseline_gltf, baseline_blob = export_gltf(build_model(recipe), clips)
        published_gltf = json.loads((folder / 'model.gltf').read_text())
        assert baseline_gltf == published_gltf and baseline_blob == (folder / 'model.bin').read_bytes(), 'Unmodified own recipe is not exact to current payload: ' + sid
        changed = proposal(sid, recipe, args.variant)
        changed_model = build_model(changed)
        gltf, blob = export_gltf(changed_model, clips)
        assert gltf['nodes'] == baseline_gltf['nodes'] and gltf['skins'] == baseline_gltf['skins'], sid
        before_png = {p.name: sha(p) for p in (folder / 'textures').glob('*.png')}
        (folder / 'model.gltf').write_text(json.dumps(gltf, indent=2) + '\n', encoding='utf8')
        (folder / 'model.bin').write_bytes(blob)
        rebaked = args.variant == 'v5' and sid == 'treeman'
        if rebaked:
            palettes = yaml.load((ROOT / 'tools/creature_palettes.yaml').read_text(encoding='utf8'), Loader=yaml.CSafeLoader)['palettes']
            palette = palette_for_recipe(changed, palettes)
            atlas = bake_surface_atlas(changed_model, palette, changed['surface_style'])
            for material in gltf['materials']:
                stem = material['name']
                texture = atlas if stem == 'body' else bake_flat(palette, stem, changed['pattern'], sid, atlas.size)
                texture.save(folder / 'textures' / (stem + '.png'))
            bake_icon(atlas).save(folder / 'textures/icon.png')
        (recipes / (sid + '.yaml')).write_text(yaml.safe_dump(changed, sort_keys=False, allow_unicode=True), encoding='utf8')
        if not rebaked:
            assert before_png == {p.name: sha(p) for p in (folder / 'textures').glob('*.png')}
        assert (folder / 'creature.json').read_bytes() == (ROOT / 'models/creatures' / sid / 'creature.json').read_bytes()
        report['species'].append({'id': sid, 'source_recipe_sha256': sha(source_recipe),
                                  'proposal_recipe_sha256': sha(recipes / (sid + '.yaml')),
                                  'model_gltf_sha256': sha(folder / 'model.gltf'),
                                  'model_bin_sha256': sha(folder / 'model.bin'),
                                  'source_textures_sha256': before_png,
                                  'textures_sha256': {p.name: sha(p) for p in (folder / 'textures').glob('*.png')},
                                  'texel_dimensions_refit_and_surface_rebaked': rebaked,
                                  'unmodified_recipe_exact_to_current_payload': True,
                                  'nodes_skin_and_definition_unchanged': True})
        print('PROPOSAL', sid, flush=True)
    (qa / 'proposal_report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')


if __name__ == '__main__':
    main()
