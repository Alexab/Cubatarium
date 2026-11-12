"""Geometric feasibility of a strict flat-paw stance, not a gait acceptance test.

Uses the existing badger rig. It intentionally tests a stronger contact contract
than toe/heel rolling: a horizontal paw with locked yaw and a prescribed center.
An empty shared-root solution rejects that particular contract, not the rig.
"""
import json
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from review_creature_redesign import accessor, sample_nodes, world_matrices
from audit_creature_motion_v5 import sha

ROOT = Path(__file__).resolve().parents[2]


def main():
    folder = ROOT / 'assetgen_output/creatures/badger_stride_v3/models/creatures/badger'
    out = ROOT / 'docs/research/creature-r6-v2/badger-support-reach-v1'
    out.mkdir(parents=True, exist_ok=True)
    g = json.loads((folder / 'model.gltf').read_text())
    blobs = [(folder / b['uri']).read_bytes() for b in g['buffers']]
    definition = json.loads((folder / 'creature.json').read_text())
    scale = definition['visual']['gltf']['model_scale']
    nodes = {n['name']: i for i, n in enumerate(g['nodes'])}
    parents = {c: i for i, n in enumerate(g['nodes']) for c in n.get('children', [])}
    bind = world_matrices(g['nodes'])
    upper_length = float(np.linalg.norm(g['nodes'][nodes['lower_fl']]['translation']))
    assert abs(upper_length - 3/16) < 1e-8
    # Lower origin -> horizontal sole reference. This is a virtual sole center,
    # not a newly inserted bone or a claim that its center is a mesh vertex.
    sole_from_lower = np.array([0., -2/16, .5/16])
    foot_specs = {}
    for suffix in ['fl', 'fr', 'bl', 'br']:
        upper, lower = nodes['upper_' + suffix], nodes['lower_' + suffix]
        assert parents[lower] == upper
        assert np.allclose(g['nodes'][lower]['translation'], [0, -upper_length, 0])
        lower_origin = bind[lower][:3, 3]
        foot_specs[suffix] = {'upper': upper, 'lower': lower,
                             'sole_bind': lower_origin + sole_from_lower,
                             'phase_offset': 0. if suffix in ['fl', 'br'] else .5}
    animation = next(a for a in g['animations'] if a['name'] == 'walk')
    duration = max(float(accessor(g, blobs, s['input'])[-1, 0]) for s in animation['samplers'])
    cycles = 14
    # Calibrate this diagnostic to the measured route only, not every speed.
    speed_world = .475
    clip_rate = .176
    travel_per_clip_second = speed_world / (scale * clip_rate)
    step = travel_per_clip_second * duration / cycles
    results = {}
    for variant in ['current_sway', 'zero_sway_control']:
        rows = []
        for phase in np.linspace(0, 1, 1025):
            sampled = sample_nodes(g, blobs, 'walk', float(phase))
            sampled[nodes['body']]['translation'] = [0., 0., 0.]
            if variant == 'zero_sway_control':
                for name in ['body', 'spine']:
                    sampled[nodes[name]]['rotation'] = [0., 0., 0., 1.]
            world = world_matrices(sampled)
            support = []
            for suffix, spec in foot_specs.items():
                local_phase = (phase * cycles + spec['phase_offset']) % 1.
                if local_phase >= .5:
                    continue
                # Locked world point during this predefined 50%-duty stance.
                # Body travels +Z; body-relative sole sweeps backwards.
                sole = spec['sole_bind'].copy()
                sole[2] += step * (.25 - local_phase)
                ankle = sole - sole_from_lower
                upper = spec['upper']
                parent = world[parents[upper]]
                hip = (parent @ np.array([*g['nodes'][upper]['translation'], 1.]))[:3]
                horizontal_squared = float(np.sum((ankle[[0, 2]] - hip[[0, 2]])**2))
                if horizontal_squared > upper_length**2:
                    support.append({'foot': suffix, 'reachable': False})
                    continue
                required_root_y = float(ankle[1] + np.sqrt(upper_length**2 - horizontal_squared) - hip[1])
                support.append({'foot': suffix, 'reachable': True, 'required_root_y': required_root_y})
            assert len(support) == 2
            reachable = all(s['reachable'] for s in support)
            spread = ((max(s['required_root_y'] for s in support) - min(s['required_root_y'] for s in support)) * scale) if reachable else None
            rows.append({'phase': float(phase), 'support': support,
                         'root_disagreement_world': spread})
        values = [r['root_disagreement_world'] for r in rows if r['root_disagreement_world'] is not None]
        results[variant] = {'unreachable_frames': sum(r['root_disagreement_world'] is None for r in rows),
                            'root_disagreement_world_max': max(values),
                            'root_disagreement_world_p50': float(np.median(values)),
                            'fraction_above_0_002_world': sum(v > .002 for v in values) / len(values),
                            'samples': rows}
    assert results['zero_sway_control']['unreachable_frames'] == 0
    assert results['zero_sway_control']['root_disagreement_world_max'] < 1e-7
    # Necessary (not sufficient) reach check for the new independent foot
    # articulation. Knee limits are a proposed diagnostic bound, not a vetted
    # species contract. Hip/foot limits, geometry and pole choice remain open.
    knee_max = np.radians(120.)
    lower_length = 1.5/16  # lower pivot Y2 -> support-articulation pivot Y0.5
    foot_height = .5/16
    maximum_reach = upper_length + lower_length
    minimum_reach = np.sqrt(upper_length**2 + lower_length**2 + 2*upper_length*lower_length*np.cos(knee_max))
    articulated = {}
    for amplitude in [0., 8., 12., 20.]:
        widths = []
        for phase in np.linspace(0, 1, 1025):
            sampled = sample_nodes(g, blobs, 'walk', float(phase))
            sampled[nodes['body']]['translation'] = [0., 0., 0.]
            q = np.array(sampled[nodes['body']]['rotation'])
            assert np.max(np.abs(q[:2])) < 1e-7
            angle = 2*np.arctan2(q[2], q[3])*amplitude/20
            sampled[nodes['body']]['rotation'] = [0., 0., float(np.sin(angle/2)), float(np.cos(angle/2))]
            world = world_matrices(sampled)
            intervals = []
            for suffix, spec in foot_specs.items():
                local_phase = (phase*cycles + spec['phase_offset']) % 1.
                if local_phase >= .5:
                    continue
                sole = spec['sole_bind'].copy(); sole[2] += step*(.25-local_phase)
                target = sole.copy(); target[1] += foot_height; target[2] -= sole_from_lower[2]
                upper = spec['upper']
                hip = (world[parents[upper]] @ np.array([*g['nodes'][upper]['translation'], 1.]))[:3]
                h2 = float(np.sum((target[[0,2]]-hip[[0,2]])**2))
                assert h2 < maximum_reach**2
                low = target[1]-hip[1]+np.sqrt(max(0., minimum_reach**2-h2))
                high = target[1]-hip[1]+np.sqrt(maximum_reach**2-h2)
                intervals.append((low,high))
            widths.append(float((min(i[1] for i in intervals)-max(i[0] for i in intervals))*scale))
        articulated[str(amplitude)] = {'root_interval_world_min_width': min(widths),
                                      'root_interval_world_median_width': float(np.median(widths)),
                                      'infeasible_samples': sum(w < 0 for w in widths), 'samples': len(widths)}
    report = {'scope': __doc__, 'acceptance': 'not_evaluated',
              'contract': {'support': 'predefined diagonal 50%-duty; fixed yaw horizontal paw',
                           'samples': 1025, 'walk_cycles': cycles, 'upper_length_model': upper_length,
                           'sole_from_lower_model': sole_from_lower.tolist(), 'world_speed': speed_world,
                           'clip_rate': clip_rate, 'stride_model_travel': step,
                           'restriction': 'No toe/heel roll, no ankle articulation, no leg stretch; hip above lower joint'},
              'input_sha256': {p.name: sha(p) for p in [folder/'model.gltf', folder/'model.bin', folder/'creature.json']},
              'analyzer_sha256': sha(Path(__file__)), 'variants': results,
              'articulated_necessary_reach': {'scope': 'Necessary root-interval intersection only, no hip/foot limits, pole continuity, actual skinned deformation, terrain or visual acceptance',
                  'lower_length_model': lower_length, 'foot_pivot_height_model': foot_height,
                  'knee_limit_degrees': [0,120], 'body_roll_amplitude_degrees': articulated}}
    (out / 'reach.json').write_text(json.dumps(report, indent=2) + '\n')
    fig, axes = plt.subplots(2, 1, figsize=(12, 7), sharex=True)
    colors = dict(zip(foot_specs, ['tab:blue', 'tab:orange', 'tab:green', 'tab:red']))
    for ax, (variant, data) in zip(axes, results.items()):
        for foot in foot_specs:
            ys = [next((s.get('required_root_y', np.nan) for s in row['support'] if s['foot'] == foot), np.nan) for row in data['samples']]
            ax.plot([r['phase'] for r in data['samples']], ys, label=foot, color=colors[foot], linewidth=1)
        ax.set_title(variant + ': root Y needed by each horizontal planted paw')
        ax.set_ylabel('Root Y [model units]'); ax.legend(ncol=4)
    axes[-1].set_xlabel('Walk macroloop phase')
    fig.suptitle('Existing rig, strict flat-paw contract feasibility only; not an IK or gait acceptance')
    fig.tight_layout(); fig.savefig(out / 'required_root.png', dpi=140); plt.close(fig)
    print(json.dumps({k:{kk:vv for kk,vv in v.items() if kk!='samples'} for k,v in results.items()}, indent=2))
    print(json.dumps(report['articulated_necessary_reach'], indent=2))


if __name__ == '__main__':
    main()
