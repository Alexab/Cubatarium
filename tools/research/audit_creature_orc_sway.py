"""Verify multi-stride sway preserves own geometry and limb timing, not art acceptance."""
import argparse
import json
from pathlib import Path
import numpy as np
import yaml
from audit_creature_motion_v5 import ROOT, inspect, sha
from review_creature_redesign import accessor, sample_nodes


def angle(a, b):
    a, b = np.array(a, float), np.array(b, float)
    dot = abs(float(a @ b / (np.linalg.norm(a) * np.linalg.norm(b))))
    return float(np.rad2deg(2 * np.arccos(np.clip(dot, 0, 1))))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--qa-name', default='orc_sway_v1')
    parser.add_argument('--source-root', type=Path, default=ROOT)
    args = parser.parse_args(); out = args.out.resolve()
    source = args.source_root / 'models/creatures/orc'
    qa = ROOT / 'assetgen_output/creatures' / args.qa_name
    candidate = qa / 'models/creatures/orc'
    g, b, stats = inspect(source); h, d, new_stats = inspect(candidate)
    assert len(g['meshes']) == len(h['meshes']) == 1
    assert len(g['meshes'][0]['primitives']) == len(h['meshes'][0]['primitives'])
    assert stats == new_stats
    assert g['nodes'] == h['nodes'] and g['skins'] == h['skins']
    assert (source / 'creature.json').read_bytes() == (candidate / 'creature.json').read_bytes()
    for x, y in zip(g['meshes'][0]['primitives'], h['meshes'][0]['primitives']):
        assert x['attributes'].keys() == y['attributes'].keys()
        for key in x['attributes']:
            assert np.array_equal(accessor(g, b, x['attributes'][key]), accessor(h, d, y['attributes'][key]))
        assert np.array_equal(accessor(g, b, x['indices']), accessor(h, d, y['indices']))
    for p in (source / 'textures').glob('*.png'): assert p.read_bytes() == (candidate / 'textures' / p.name).read_bytes()
    old_recipe = yaml.safe_load((args.source_root / 'tools/creature_recipes/orc.yaml').read_text(encoding='utf-8'))
    recipe = yaml.safe_load((qa / 'recipes/orc.yaml').read_text(encoding='utf-8'))
    assert old_recipe['anatomy'] == recipe['anatomy']
    root = next(i for i, n in enumerate(g['nodes']) if n.get('name') == 'body')
    rows = []; exact = 0
    for a, c in zip(g['animations'], h['animations']):
        assert a['name'] == c['name']
        name = a['name']
        if name not in ('walk', 'run'):
            assert a['channels'] == c['channels']
            for x, y in zip(a['samplers'], c['samplers']):
                for key in ('input', 'output'): assert np.array_equal(accessor(g, b, x[key]), accessor(h, d, y[key]))
                exact += 1
            continue
        old_duration = max(float(accessor(g, b, s['input'])[-1, 0]) for s in a['samplers'])
        duration = max(float(accessor(h, d, s['input'])[-1, 0]) for s in c['samplers'])
        cycles = recipe['motion']['clips'][name]['gait_cycles_per_loop']
        assert abs(duration / old_duration - cycles) < 1e-5
        assert recipe['motion']['clips'][name]['runtime_speed'] == old_recipe['motion']['clips'][name]['runtime_speed']
        maximum = 0.; translation = 0.
        for phase in np.linspace(0, 1, cycles * 256 + 1):
            old_phase = (float(phase) * duration % old_duration) / old_duration
            before = sample_nodes(g, b, name, old_phase); after = sample_nodes(h, d, name, float(phase))
            for i, (x, y) in enumerate(zip(before, after)):
                if i != root:
                    maximum = max(maximum, angle(x.get('rotation', [0, 0, 0, 1]), y.get('rotation', [0, 0, 0, 1])))
                ax, bx = np.array(x.get('translation', [0, 0, 0])), np.array(y.get('translation', [0, 0, 0]))
                translation = max(translation, float(np.abs((ax - bx)[[0, 2]] if i == root else ax - bx).max()))
        assert maximum < .001 and translation < 1e-6, (name, maximum, translation)
        first, last = sample_nodes(h, d, name, 0.), sample_nodes(h, d, name, 1.)
        for x, y in zip(first, last):
            assert angle(x.get('rotation', [0, 0, 0, 1]), y.get('rotation', [0, 0, 0, 1])) < .001
            assert np.max(np.abs(np.array(x.get('translation', [0, 0, 0])) - y.get('translation', [0, 0, 0]))) < 1e-5
        speed = recipe['motion']['clips'][name]['runtime_speed']
        rows.append({'clip': name, 'stride_cycles_per_loop': cycles, 'stride_period_seconds_at_unit_travel': old_duration / speed,
                     'sway_period_seconds_at_unit_travel': duration / speed,
                     'nonroot_rotation_error_degrees': maximum, 'unchanged_translation_components_error_blocks': translation})
    transitions = json.loads((out / 'native_current/transition_probe.json').read_text())['resources'][0]
    frames = [f for c in transitions['cases'] for f in c['frames']]
    floor = [f['low'][1] for c in transitions['cases'] if c['supported'] for f in c['frames']]
    assert floor and min(floor) >= -.001 and all(f['visual_envelope_covers_geometry'] for f in frames)
    report = {'passed': True, 'scope': 'Own mesh/UV/skin/texture exact, repeated limb timing at equal elapsed time, new body roll/root clearance and sampled native transitions; not art/stride/sliding/performance acceptance',
              'nodes_skin_geometry_textures_definition_exact': True, 'source_geometry': stats, 'candidate_geometry': new_stats,
              'unchanged_idle_action_samplers': exact, 'macroloops': rows, 'transition_frames': len(frames),
              'local_loop_seams_C0_verified': True, 'source_root': str(args.source_root.resolve()),
              'source_sha256': {str(p.relative_to(args.source_root)): sha(p) for p in [source / 'model.gltf', source / 'model.bin', source / 'creature.json', args.source_root / 'tools/creature_recipes/orc.yaml']},
              'builder_sha256': sha(Path(__file__)),
              'supported_frames': len(floor), 'supported_min_y': min(floor),
              'payload_sha256': {str(p.relative_to(candidate)): sha(p) for p in candidate.rglob('*') if p.is_file()}}
    (out / 'motion_invariant_audit.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k != 'payload_sha256'}, indent=2))


if __name__ == '__main__': main()
