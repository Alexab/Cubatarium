"""Isolated own-recipe oerkki mass proposal; no reference resources read."""
import argparse
from copy import deepcopy
import json
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


def proposal(recipe):
    changed = deepcopy(recipe)
    edits = []
    for bone in changed['anatomy']['bones']:
        for cube in bone.get('cubes', []):
            name = cube['name']
            dimensions = {}
            if name == 'core':
                dimensions = {0: 7, 2: 5}
            elif name == 'chest':
                dimensions = {2: 5}
            elif name == 'head':
                dimensions = {0: 7}
                cube['shape']['top_scale'] = [.96, .96]
            elif name.startswith(('upper_arm_', 'forearm_')):
                dimensions = {0: 3, 2: 4}
            elif name.startswith(('thigh_', 'shin_')):
                dimensions = {2: 4}
            for axis, size in dimensions.items():
                old = cube['size'][axis]
                cube['origin'][axis] -= (size - old) / 2
                cube['size'][axis] = size
                edits.append({'cube': name, 'axis': 'xyz'[axis], 'before': old, 'after': size})
    assert changed['motion'] == recipe['motion']
    assert [(b['name'], b['parent'], b['pivot']) for b in changed['anatomy']['bones']] == [(b['name'], b['parent'], b['pivot']) for b in recipe['anatomy']['bones']]
    return changed, edits


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qa-name', required=True)
    parser.add_argument('--source-root', type=Path, default=ROOT)
    args = parser.parse_args()
    assert Path(args.qa_name).name == args.qa_name and args.qa_name not in ('.', '..')
    out = ROOT / 'assetgen_output/creatures' / args.qa_name
    assert not out.exists(), 'Fresh proposal folder required'
    models = out / 'models'; models.mkdir(parents=True)
    for kind in ('creatures', 'skins'):
        shutil.copytree(ROOT / 'models' / kind, models / kind, ignore=shutil.ignore_patterns('asset_manifest.json'))
    folder = models / 'creatures/oerkki'
    source = args.source_root / 'tools/creature_recipes/oerkki.yaml'
    if args.source_root.resolve() != ROOT:
        shutil.copytree(args.source_root / 'models/creatures/oerkki', folder, dirs_exist_ok=True)
    recipe = yaml.load(source.read_text(encoding='utf-8'), Loader=yaml.CSafeLoader)
    old = json.loads((folder / 'model.gltf').read_text())
    clips = [a['name'] for a in old['animations']]
    before, blob = export_gltf(build_model(recipe), clips)
    assert before == old and blob == (folder / 'model.bin').read_bytes()
    changed, edits = proposal(recipe)
    model = build_model(changed)
    after, data = export_gltf(model, clips)
    assert before['nodes'] == after['nodes'] and before['skins'] == after['skins']
    palettes = yaml.load((ROOT / 'tools/creature_palettes.yaml').read_text(encoding='utf-8'), Loader=yaml.CSafeLoader)['palettes']
    palette = palette_for_recipe(changed, palettes)
    atlas = bake_surface_atlas(model, palette, changed['surface_style'])
    for material in after['materials']:
        name = material['name']
        texture = atlas if name == 'body' else bake_flat(palette, name, changed['pattern'], 'oerkki', atlas.size)
        texture.save(folder / 'textures' / (name + '.png'))
    bake_icon(atlas).save(folder / 'textures/icon.png')
    (folder / 'model.gltf').write_text(json.dumps(after, indent=2) + '\n', encoding='utf-8')
    (folder / 'model.bin').write_bytes(data)
    (out / 'recipes').mkdir()
    recipe_path = out / 'recipes/oerkki.yaml'
    recipe_path.write_text(yaml.safe_dump(changed, sort_keys=False, allow_unicode=True), encoding='utf-8')
    report = {'schema': 'creature-anatomy-proposal.v1', 'state': 'unapproved_proposal', 'id': 'oerkki',
              'source_recipe_sha256': sha(source), 'builder_sha256': sha(Path(__file__)),
              'nodes_skin_definition_motion_unchanged': True, 'edits': edits,
              'files_sha256': {str(p.relative_to(out)): sha(p) for p in [recipe_path, *folder.rglob('*')] if p.is_file()}}
    (out / 'proposal_report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('UNAPPROVED', out)


if __name__ == '__main__': main()
