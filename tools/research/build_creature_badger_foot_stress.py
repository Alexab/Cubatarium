"""Non-gameplay foot-articulation stress fixture; never a locomotion candidate."""
from copy import deepcopy
import json
from pathlib import Path
import shutil
import yaml
from audit_creature_motion_v5 import sha
from creaturegen.anatomy import build_model
from creaturegen.export_gltf import export_gltf

ROOT = Path(__file__).resolve().parents[2]


def main():
    source = ROOT / 'assetgen_output/creatures/badger_ankle_control_v1'
    out = ROOT / 'assetgen_output/creatures/badger_foot_stress_v1'
    assert not out.exists()
    recipe = yaml.safe_load((source / 'recipes/badger.yaml').read_text(encoding='utf8'))
    changed = deepcopy(recipe)
    walk = changed['motion']['clips']['walk']
    duration = max(t['times'][-1] for t in walk['tracks'])
    for suffix in ['fl', 'fr', 'bl', 'br']:
        side = 1 if suffix.endswith('l') else -1
        walk['tracks'].append({'bone': 'foot_' + suffix, 'path': 'rotation',
            'times': [duration*i/4 for i in range(5)],
            'values': [[0,0,0], [30,0,side*20], [0,0,0], [-30,0,-side*20], [0,0,0]]})
    original = json.loads((source / 'models/creatures/badger/model.gltf').read_text())
    g, blob = export_gltf(build_model(changed), [a['name'] for a in original['animations']])
    out.mkdir(parents=True)
    shutil.copytree(source / 'models', out / 'models')
    folder = out / 'models/creatures/badger'
    (folder / 'model.gltf').write_text(json.dumps(g, indent=2) + '\n')
    (folder / 'model.bin').write_bytes(blob)
    (out / 'recipes').mkdir()
    (out / 'recipes/badger.yaml').write_text(yaml.safe_dump(changed, sort_keys=False), encoding='utf8')
    # Identical no-natural-population fixture setting for actual-world startup.
    for p in (out / 'models/creatures').glob('*/creature.json'):
        d = json.loads(p.read_text(encoding='utf8')); d['natural_spawnable'] = False
        p.write_text(json.dumps(d, indent=2, ensure_ascii=False)+'\n', encoding='utf8')
    report = {'scope': __doc__, 'state': 'diagnostic_only_not_gait_candidate',
              'foot_euler_stress_degrees': {'x': [-30,30], 'z': [-20,20]},
              'source_report_sha256': sha(source / 'proposal_report.json'),
              'builder_sha256': sha(Path(__file__)),
              'root_y_unchanged': True, 'floor_acceptance': 'not_evaluated; deliberate unsupported stress rotations',
              'proposal_sha256': {p.name:sha(p) for p in [folder/'model.gltf', folder/'model.bin', out/'recipes/badger.yaml']}}
    (out / 'stress_report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2), flush=True)


if __name__ == '__main__':
    main()
