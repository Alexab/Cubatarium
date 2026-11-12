"""Isolated own badger multi-rate proposal. No reference resources enter generation."""
import argparse
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
from build_creature_anatomy_pilots import sha


def proposal(recipe):
    changed = deepcopy(recipe); edits = []
    for name, cycles, amplitude in [('walk', 7, 20), ('run', 9, 24)]:
        clip = changed['motion']['clips'][name]
        duration = max(t['times'][-1] for t in clip['tracks'])
        body_roll = [t for t in clip['tracks'] if t['bone'] == 'body' and t['path'] == 'rotation']
        assert len(body_roll) == 1
        clip['tracks'].remove(body_roll[0])
        for track in clip['tracks']:
            assert track['times'][0] == 0 and track['times'][-1] == duration
            assert max(abs(a-b) for a,b in zip(track['values'][0], track['values'][-1])) < 1e-4
            times, values = track['times'], track['values']
            track['times'] = [round(cycle*duration+t, 8) for cycle in range(cycles)
                              for i,t in enumerate(times) if cycle == 0 or i > 0]
            track['values'] = [deepcopy(v) for cycle in range(cycles)
                               for i,v in enumerate(values) if cycle == 0 or i > 0]
        end = duration * cycles
        clip['tracks'].append({'bone': 'body', 'path': 'rotation',
            'times': [round(end*i/128, 8) for i in range(129)],
            'values': [[0.,0.,round(amplitude*math.sin(2*math.pi*i/128), 6)] for i in range(129)]})
        # Only root translation is densely baked by ground contact; limb keys
        # remain the original repeated SLERP segments. Not a dense rig resample.
        contact_steps = 256 if name == 'walk' else 224
        assert cycles*contact_steps+1 <= 2049
        clip['contact'].update(geometry='skinned_mesh', samples=cycles*contact_steps+1)
        clip['gait_cycles_per_loop'] = cycles; clip['sway_cycles_per_loop'] = 1
        edits.append({'clip': name, 'stride_asset_seconds': duration,
                      'macroloop_asset_seconds': end, 'stride_cycles': cycles,
                      'roll_degrees': amplitude, 'root_contact_samples': cycles*contact_steps+1})
    assert changed['anatomy'] == recipe['anatomy']
    return changed, edits


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qa-name', required=True)
    args = parser.parse_args()
    assert Path(args.qa_name).name == args.qa_name and args.qa_name not in ('.', '..')
    out = ROOT / 'assetgen_output/creatures' / args.qa_name
    assert not out.exists(), 'Fresh proposal directory required'
    out.mkdir(parents=True)
    for kind in ['creatures', 'skins']:
        shutil.copytree(ROOT / 'models' / kind, out / 'models' / kind,
                        ignore=shutil.ignore_patterns('asset_manifest.json'))
    source = ROOT / 'tools/creature_recipes/badger.yaml'
    recipe = yaml.load(source.read_text(encoding='utf8'), Loader=yaml.CSafeLoader)
    folder = out / 'models/creatures/badger'
    old = json.loads((folder / 'model.gltf').read_text()); names = [a['name'] for a in old['animations']]
    before, blob = export_gltf(build_model(recipe), names)
    assert before == old and blob == (folder / 'model.bin').read_bytes(), 'Live recipe must reproduce source'
    changed, edits = proposal(recipe)
    after, data = export_gltf(build_model(changed), names)
    assert before['nodes'] == after['nodes'] and before['skins'] == after['skins']
    (folder / 'model.gltf').write_text(json.dumps(after, indent=2)+'\n', encoding='utf8')
    (folder / 'model.bin').write_bytes(data)
    (out / 'recipes').mkdir()
    recipe_path = out / 'recipes/badger.yaml'
    recipe_path.write_text(yaml.safe_dump(changed, sort_keys=False, allow_unicode=True), encoding='utf8')
    report = {'state': 'unapproved_proposal', 'id': 'badger', 'builder_sha256': sha(Path(__file__)),
              'source_recipe_sha256': sha(source), 'source_gltf_sha256': sha(ROOT/'models/creatures/badger/model.gltf'),
              'source_bin_sha256': sha(ROOT/'models/creatures/badger/model.bin'), 'edits': edits,
              'before_bin_bytes': len(blob), 'candidate_bin_bytes': len(data),
              'files_sha256': {str(p.relative_to(out)): sha(p) for p in [recipe_path, *folder.rglob('*')] if p.is_file()}}
    (out / 'proposal_report.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf8')
    print('UNAPPROVED', out, len(blob), '->', len(data), flush=True)


if __name__ == '__main__':
    main()
