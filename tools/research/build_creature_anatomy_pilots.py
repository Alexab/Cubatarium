"""Build isolated, unapproved own-recipe anatomy proposals; no references read."""
import argparse
from copy import deepcopy
import hashlib
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


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qa-name', required=True)
    parser.add_argument('--variant', choices=('v1', 'v2'), default='v1')
    parser.add_argument('--source-root', type=Path, default=ROOT, help='Project-shaped own snapshot to reproduce proposals after publication')
    args = parser.parse_args()
    assert Path(args.qa_name).name == args.qa_name and args.qa_name not in ('.', '..')
    out = ROOT / 'assetgen_output/creatures' / args.qa_name
    assert not out.exists(), 'Fresh proposal folder required'
    models = out / 'models'
    models.mkdir(parents=True)
    for kind in ('creatures', 'skins'):
        shutil.copytree(ROOT / 'models' / kind, models / kind,
                        ignore=shutil.ignore_patterns('asset_manifest.json'))
    sid = 'penguin'
    if args.source_root.resolve() != ROOT:
        shutil.copytree(args.source_root / 'models/creatures' / sid, models / 'creatures' / sid, dirs_exist_ok=True)
    source = args.source_root / 'tools/creature_recipes' / (sid + '.yaml')
    recipe = yaml.load(source.read_text(encoding='utf-8'), Loader=yaml.CSafeLoader)
    definition = json.loads((models / 'creatures' / sid / 'creature.json').read_text())
    clips = list(dict.fromkeys(definition['visual']['animation']['state_map'].values()))
    before, before_bin = export_gltf(build_model(recipe), clips)
    folder = models / 'creatures' / sid
    assert before == json.loads((folder / 'model.gltf').read_text())
    assert before_bin == (folder / 'model.bin').read_bytes()
    changed = deepcopy(recipe)
    body = next(b for b in changed['anatomy']['bones'] if b['name'] == 'body')
    trunk = next(c for c in body['cubes'] if c['name'] == 'trunk')
    assert trunk['origin'] == [-3.5, 2, -1] and trunk['size'] == [7, 9, 5]
    trunk['origin'][2] = -4 if args.variant == 'v1' else -2
    trunk['size'][2] = 6
    model = build_model(changed)
    after, after_bin = export_gltf(model, clips)
    assert after['nodes'] == before['nodes'] and after['skins'] == before['skins']
    assert changed['motion'] == recipe['motion']
    palettes = yaml.load((ROOT / 'tools/creature_palettes.yaml').read_text(encoding='utf-8'), Loader=yaml.CSafeLoader)['palettes']
    palette = palette_for_recipe(changed, palettes)
    atlas = bake_surface_atlas(model, palette, changed['surface_style'])
    for material in after['materials']:
        name = material['name']
        texture = atlas if name == 'body' else bake_flat(palette, name, changed['pattern'], sid, atlas.size)
        texture.save(folder / 'textures' / (name + '.png'))
    bake_icon(atlas).save(folder / 'textures/icon.png')
    (folder / 'model.gltf').write_text(json.dumps(after, indent=2) + '\n', encoding='utf-8')
    (folder / 'model.bin').write_bytes(after_bin)
    recipes = out / 'recipes'
    recipes.mkdir()
    (recipes / (sid + '.yaml')).write_text(yaml.safe_dump(changed, sort_keys=False, allow_unicode=True), encoding='utf-8')
    assert (folder / 'creature.json').read_bytes() == (args.source_root / 'models/creatures' / sid / 'creature.json').read_bytes()
    report = {'schema': 'creature-anatomy-proposal.v1', 'state': 'unapproved_proposal',
              'id': sid, 'variant': 'penguin_belly_' + args.variant, 'source_recipe_sha256': sha(source),
              'builder_sha256': sha(Path(__file__)), 'source_exact_to_current': True,
              'nodes_skin_definition_motion_unchanged': True,
              'change': f'Trunk origin Z -1 to {trunk["origin"][2]} and depth5 to6; refit UV dimensions, rebake own penguin surface. Preserve pivots and weights policy.',
              'files_sha256': {str(p.relative_to(out)): sha(p) for p in [recipes / (sid + '.yaml'), *folder.rglob('*')] if p.is_file()}}
    (out / 'proposal_report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('UNAPPROVED', out)


if __name__ == '__main__':
    main()
