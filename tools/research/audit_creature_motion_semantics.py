"""Read-only state/clip/skin/timing diagnosis before motion revisions."""
import argparse
import json
from pathlib import Path
import subprocess
import numpy as np
from review_creature_redesign import ROOT, accessor
from analyze_creature_anatomy_shapes import geometry, sha


def inspect(folder):
    definition = json.loads((folder / 'creature.json').read_text())
    g = json.loads((folder / 'model.gltf').read_text())
    blobs = [(folder / b['uri']).read_bytes() for b in g['buffers']]
    joints, weights = [], []
    for mesh in g['meshes']:
        for primitive in mesh['primitives']:
            joints.append(accessor(g, blobs, primitive['attributes']['JOINTS_0']).astype(int))
            weights.append(accessor(g, blobs, primitive['attributes']['WEIGHTS_0']))
    j, w = np.concatenate(joints), np.concatenate(weights)
    masses = np.array([(w * (j == i)).sum() for i in range(len(g['skins'][0]['joints']))])
    dominant = int(masses.argmax()); influence = (w * (j == dominant)).sum(1)
    node = g['skins'][0]['joints'][dominant]
    states = {}
    for state in ('Walk', 'Run', 'Fly', 'Action'):
        clip = definition['visual']['animation']['state_map'][state]
        animation = next(a for a in g['animations'] if a['name'] == clip)
        duration = max(float(accessor(g, blobs, s['input'])[-1, 0]) for s in animation['samplers'])
        speed = definition['visual']['animation']['clips'].get(clip, {}).get('speed', 1)
        arcs = []
        for channel in animation['channels']:
            if channel['target']['path'] != 'rotation': continue
            values = accessor(g, blobs, animation['samplers'][channel['sampler']]['output']).astype(float)
            values /= np.linalg.norm(values, axis=1, keepdims=True)
            arc = float(np.rad2deg(2 * np.arccos(np.clip(np.abs(values @ values.T).min(), 0, 1))))
            if arc > 1:
                arcs.append({'node': channel['target']['node'], 'name': g['nodes'][channel['target']['node']].get('name'), 'max_key_pair_angle_deg': arc})
        states[state] = {'clip': clip, 'asset_seconds': duration, 'runtime_speed': speed,
                         'effective_loop_seconds_at_unit_travel': duration / speed, 'rotation_arcs': arcs}
    shape, _, _ = geometry(folder)
    return {'files_sha256': shape['source_sha256'], 'vertex_count_with_duplicates': len(j),
            'dominant_joint': node, 'dominant_joint_name': g['nodes'][node].get('name'),
            'dominant_joint_weight_min': float(influence.min()), 'dominant_joint_weight_max': float(influence.max()),
            'other_joint_weight_max': float(np.where(j != dominant, w, 0).max()),
            'states': states, 'sampled_state_geometry': shape['states']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args(); out = args.out.resolve()
    assert out.is_relative_to((ROOT / 'docs/research').resolve()) and not out.exists()
    out.mkdir(parents=True)
    baseline = ROOT / 'assetgen_output/creatures/review_2026_11_10/baseline/models/creatures'
    rows = {}
    for sid in ('orc', 'puffin', 'badger', 'treeman'):
        for name in ('model.gltf', 'model.bin', 'creature.json'):
            original = subprocess.check_output(['git', 'show', f'b35f00a75:models/creatures/{sid}/{name}'], cwd=ROOT)
            archived = (baseline / sid / name).read_bytes()
            if name == 'model.bin':
                assert original == archived
            else:
                assert original.replace(b'\r\n', b'\n') == archived.replace(b'\r\n', b'\n')
        rows[sid] = {label: inspect(root / sid) for label, root in [('reference', baseline), ('own', ROOT / 'models/creatures')]}
    report = {'scope': 'Read-only actual exported skin weights, clip mapping/timing and65-phase geometry; not art/stride/performance acceptance',
              'reference_commit': 'b35f00a75', 'original_git_bin_byte_exact_json_newline_only_difference': True, 'builder_sha256': sha(Path(__file__)), 'resources': rows}
    (out / 'diagnosis.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('4-species source/state/skin/timing diagnosis saved')


if __name__ == '__main__': main()
