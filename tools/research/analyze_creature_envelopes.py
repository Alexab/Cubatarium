"""Research-only original/current skinned envelopes; never changes production assets."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import numpy as np
from review_creature_redesign import ROOT, accessor, sample_nodes, world_matrices


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def measure(folder, samples):
    definition = read(folder / 'creature.json')
    g = read(folder / 'model.gltf')
    blobs = [(folder / b['uri']).read_bytes() for b in g['buffers']]
    assert len(g['skins']) == 1
    skin = g['skins'][0]
    inverse = accessor(g, blobs, skin['inverseBindMatrices']).reshape(-1, 4, 4).transpose(0, 2, 1)
    pos, joint, weight = [], [], []
    for mesh in g['meshes']:
        for primitive in mesh['primitives']:
            a = primitive['attributes']
            pos.append(accessor(g, blobs, a['POSITION']))
            joint.append(accessor(g, blobs, a['JOINTS_0']).astype(int))
            weight.append(accessor(g, blobs, a['WEIGHTS_0']))
    pos, joint, weight = np.concatenate(pos), np.concatenate(joint), np.concatenate(weight)
    homogeneous = np.column_stack([pos, np.ones(len(pos))])
    config = definition['visual'].get('gltf', {})
    scale = config.get('model_scale', 1)
    yaw = math.radians(config.get('model_yaw_offset_deg', definition['visual'].get('model_yaw_offset_deg', 0)))
    rotation = np.array([[math.cos(yaw), 0, math.sin(yaw)], [0, 1, 0], [-math.sin(yaw), 0, math.cos(yaw)]])
    offset = config.get('model_offset_y', 0) - float(pos[:, 1].min()) * scale
    clips = {}
    overall_low = np.full(3, np.inf)
    overall_high = -overall_low
    for clip in [None] + [a['name'] for a in g.get('animations', [])]:
        values = []
        for phase in ([0.] if clip is None else np.linspace(0, 1, samples)):
            world = world_matrices(sample_nodes(g, blobs, clip, float(phase)))
            palette = np.array([world[node] @ inv for node, inv in zip(skin['joints'], inverse)])
            matrices = (palette[joint] * weight[:, :, None, None]).sum(axis=1)
            posed = np.einsum('nij,nj->ni', matrices, homogeneous)[:, :3] * scale
            posed = posed @ rotation.T
            posed[:, 1] += offset
            low, high = posed.min(axis=0), posed.max(axis=0)
            values.append({'phase': float(phase), 'low': low.tolist(), 'high': high.tolist(), 'size': (high - low).tolist()})
            overall_low = np.minimum(overall_low, low)
            overall_high = np.maximum(overall_high, high)
        duration = 0.
        if clip is not None:
            animation = next(a for a in g['animations'] if a['name'] == clip)
            duration = max(float(accessor(g, blobs, s['input'])[-1, 0]) for s in animation['samplers'])
        clips[clip or 'bind'] = {'duration': duration, 'samples': values,
                               'max_extent': np.max([v['size'] for v in values], axis=0).tolist(),
                               'union_low': np.min([v['low'] for v in values], axis=0).tolist(),
                               'union_high': np.max([v['high'] for v in values], axis=0).tolist()}
    return {'bounds': definition['bounds'], 'eye_height': definition['eye_height'],
            'runtime_root_offset_y': offset, 'model_scale': scale,
            'state_map': definition['visual'].get('animation', {}).get('state_map', {}),
            'clip_settings': definition['visual'].get('animation', {}).get('clips', {}),
            'clips': clips, 'all_pose_low': overall_low.tolist(), 'all_pose_high': overall_high.tolist(),
            'source_sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                              for p in [folder / 'creature.json', folder / 'model.gltf', folder / 'model.bin']}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--samples', type=int, default=129)
    parser.add_argument('--species', help='Explicit research subset, default the17 discovered glTF discrepancies')
    parser.add_argument('--current-models', type=Path, default=ROOT / 'models/creatures')
    parser.add_argument('--reference-models', type=Path, default=ROOT / 'assetgen_output/creatures/review_2026_11_10/baseline/models/creatures', help='Research-only reference root; explicit alignment copies must retain provenance')
    args = parser.parse_args()
    assert args.samples >= 65
    out = args.out.resolve()
    assert out.is_relative_to((ROOT / 'docs/research').resolve())
    out.mkdir(parents=True, exist_ok=True)
    ids = args.species.split(',') if args.species else read(ROOT / 'docs/research/creature-r6-v2/save-roundtrip/bounds_diagnostic.json')['mismatching_species']
    current_models = args.current_models.resolve()
    baseline = args.reference_models.resolve()
    report = {'schema': 'creature-original-current-envelopes.v1',
              'scope': 'Offline exact exported skinning, all asset clips; sampled not analytic envelope and not native/art/physics acceptance',
              'samples_per_clip': args.samples, 'current_models_root': str(current_models), 'reference_models_root': str(baseline), 'resources': {}}
    profile = {'schema': 'creature-native-envelope-profile.v1', 'resources': {}}
    for sid in ids:
        versions = {name: measure(folder / sid, args.samples)
                    for name, folder in [('baseline', baseline), ('current', current_models)]}
        assert versions['baseline']['bounds'] == versions['current']['bounds']
        report['resources'][sid] = versions
        collision = versions['current']['bounds']['rest']
        low = np.minimum(np.min([v['all_pose_low'] for v in versions.values()], axis=0), [-collision[0] / 2, 0, -collision[2] / 2])
        high = np.maximum(np.max([v['all_pose_high'] for v in versions.values()], axis=0), [collision[0] / 2, collision[1], collision[2] / 2])
        frames = []
        for state in ('Idle', 'Walk', 'Run', 'Action', 'Fly', 'Glide'):
            clips = {name: version['state_map'].get(state, 'idle') for name, version in versions.items()}
            if any(clip not in versions[name]['clips'] for name, clip in clips.items()):
                continue
            if state in ('Run', 'Action', 'Fly', 'Glide') and all(clips[name] == version['state_map'].get('Walk') for name, version in versions.items()):
                continue
            if state in ('Fly', 'Glide') and all(clips[name] == version['state_map'].get('Idle') for name, version in versions.items()):
                continue
            phases = {0., .25, .5, .75}
            for name, clip in clips.items():
                samples = versions[name]['clips'][clip]['samples']
                for axis in range(3):
                    phases.add(max(samples, key=lambda s: s['size'][axis])['phase'])
            for phase in sorted(phases):
                for view, yaw, pitch in [('front', 0., 0.), ('side', 90., 0.), ('oblique', 35., 15.)]:
                    frames.append({'state': state, 'phase': phase, 'view': view, 'yaw': yaw, 'pitch': pitch,
                                   'expected_clips': clips})
        profile['resources'][sid] = {'framing_low': (low - .05).tolist(), 'framing_high': (high + .05).tolist(),
                                     'collision': collision, 'frames': frames}
        print(sid, 'full_extents', {name: [round(a, 3) for a in np.array(v['all_pose_high']) - v['all_pose_low']] for name, v in versions.items()}, flush=True)
        (out / 'offline_metrics.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
        (out / 'native_profile.json').write_text(json.dumps(profile, indent=2) + '\n', encoding='utf8')


if __name__ == '__main__':
    main()
