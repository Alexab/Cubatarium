"""Independent exported-data audit of the unpublished Walk cadence proposal."""
import json
from pathlib import Path
import sys
import numpy as np
import yaml
from audit_creature_motion_v5 import inspect, sha
from review_creature_redesign import accessor, sample_nodes, world_matrices

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from creaturegen.anatomy import build_model
from creaturegen.export_gltf import export_gltf


def channels(g, blobs):
    result = {}
    for animation in g['animations']:
        for c in animation['channels']:
            s = animation['samplers'][c['sampler']]
            key = (animation['name'], g['nodes'][c['target']['node']]['name'], c['target']['path'])
            result[key] = (s.get('interpolation', 'LINEAR'), accessor(g, blobs, s['input']), accessor(g, blobs, s['output']))
    return result


def main():
    source = ROOT / 'assetgen_output/creatures/badger_sway_v2/models/creatures/badger'
    proposal = ROOT / 'assetgen_output/creatures/badger_stride_v3'
    folder = proposal / 'models/creatures/badger'
    a, ab, _ = inspect(source)
    b, bb, geometry = inspect(folder)
    assert a['nodes'] == b['nodes'] and a['skins'] == b['skins']
    assert a['materials'] == b['materials'] and a['images'] == b['images']
    assert len(a['meshes']) == len(b['meshes'])
    for ma, mb in zip(a['meshes'], b['meshes']):
        assert len(ma['primitives']) == len(mb['primitives'])
        for pa, pb in zip(ma['primitives'], mb['primitives']):
            assert pa['attributes'].keys() == pb['attributes'].keys()
            for attribute in pa['attributes']:
                assert np.array_equal(accessor(a, ab, pa['attributes'][attribute]), accessor(b, bb, pb['attributes'][attribute]))
            assert np.array_equal(accessor(a, ab, pa['indices']), accessor(b, bb, pb['indices']))
    for image in a['images']:
        assert (source / image['uri']).read_bytes() == (folder / image['uri']).read_bytes()
    assert (source / 'creature.json').read_bytes() == (folder / 'creature.json').read_bytes()
    ac, bc = channels(a, ab), channels(b, bb)
    assert ac.keys() == bc.keys()
    untouched, phase_shapes = [], []
    for key in ac:
        if key[0] == 'walk' and key[1].startswith(('upper_', 'lower_')):
            si, st, sv = ac[key]; ti, tt, tv = bc[key]
            assert si == ti and len(st) == 57 and len(tt) == 113
            assert np.array_equal(tv, np.concatenate([sv[:-1], sv]))
            assert np.max(np.abs(tt[:, 0] - np.linspace(0, float(st[-1, 0]), 113))) < 2e-7
            phase_shapes.append('/'.join(key))
        elif key != ('walk', 'body', 'translation'):
            assert ac[key][0] == bc[key][0]
            assert all(np.array_equal(x, y) for x, y in zip(ac[key][1:], bc[key][1:]))
            untouched.append('/'.join(key))
    recipe = yaml.safe_load((proposal / 'recipes/badger.yaml').read_text(encoding='utf8'))
    regenerated, blob = export_gltf(build_model(recipe), [x['name'] for x in b['animations']])
    assert regenerated == b and blob == bb[0]
    skin = b['skins'][0]
    inverse = accessor(b, bb, skin['inverseBindMatrices']).reshape(-1, 4, 4).transpose(0, 2, 1)
    positions, joints, weights = [], [], []
    for mesh in b['meshes']:
        for p in mesh['primitives']:
            attrs = p['attributes']
            positions.append(accessor(b, bb, attrs['POSITION']))
            joints.append(accessor(b, bb, attrs['JOINTS_0']).astype(int))
            weights.append(accessor(b, bb, attrs['WEIGHTS_0']))
    positions, joints, weights = map(np.concatenate, (positions, joints, weights))
    bind_y = positions[:, 1].min()
    homogeneous = np.column_stack([positions, np.ones(len(positions))])
    definition = json.loads((folder / 'creature.json').read_text())
    scale = definition['visual']['gltf'].get('model_scale', 1)
    offset = definition['visual']['gltf'].get('model_offset_y', 0)
    floors = []
    for phase in np.linspace(0, 1, 8193):
        world = world_matrices(sample_nodes(b, bb, 'walk', float(phase)))
        palette = np.array([world[node] @ inv for node, inv in zip(skin['joints'], inverse)])
        matrices = (palette[joints] * weights[:, :, None, None]).sum(axis=1)
        posed = np.einsum('nij,nj->ni', matrices, homogeneous)
        floors.append(float((posed[:, 1].min() - bind_y) * scale + offset))
    assert min(floors) >= -.001 and max(floors) <= .001
    report = {'scope': 'Exported rig/mesh/UV/PNG and phase-shape invariants; sampled vertical clearance, not footplant or artistic acceptance',
              'accepted': False, 'invariants_pass': True, 'geometry': geometry,
              'unchanged_channels': untouched, 'doubled_cadence_same_phase_shapes': phase_shapes,
              'recipe_reproduces_export': True, 'walk_floor_samples': len(floors),
              'walk_floor_world_min': min(floors), 'walk_floor_world_max': max(floors),
              'analyzer_sha256': sha(Path(__file__)),
              'source_sha256': sha(source / 'model.bin'), 'proposal_sha256': sha(folder / 'model.bin')}
    out = ROOT / 'docs/research/creature-r6-v2/badger-stance-v3/invariants.json'
    out.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k:v for k,v in report.items() if not isinstance(v,list)}, indent=2), flush=True)


if __name__ == '__main__':
    main()
