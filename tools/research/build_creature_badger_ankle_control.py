"""Unpublished articulation control: add four foot joints, preserve posed geometry.

This is an identity control for the next constrained gait experiment. It does
not claim to fix sliding, introduce an accepted rig, or modify CreatureGen.
"""
from copy import deepcopy
import json
from pathlib import Path
import shutil
import numpy as np
import yaml
from audit_creature_motion_v5 import inspect, sha
from review_creature_redesign import accessor, sample_nodes, world_matrices
from creaturegen.anatomy import build_model
from creaturegen.export_gltf import export_gltf

ROOT = Path(__file__).resolve().parents[2]


def geometry(g, blobs, clip, phase):
    world = world_matrices(sample_nodes(g, blobs, clip, phase))
    skin = g['skins'][0]
    inv = accessor(g, blobs, skin['inverseBindMatrices']).reshape(-1, 4, 4).transpose(0, 2, 1)
    palette = np.array([world[i] @ m for i, m in zip(skin['joints'], inv)])
    result = []
    for mesh in g['meshes']:
        for primitive in mesh['primitives']:
            a = primitive['attributes']
            p = accessor(g, blobs, a['POSITION'])
            j = accessor(g, blobs, a['JOINTS_0']).astype(int)
            w = accessor(g, blobs, a['WEIGHTS_0'])
            matrix = (palette[j] * w[:, :, None, None]).sum(axis=1)
            result.append(np.einsum('nij,nj->ni', matrix, np.column_stack([p, np.ones(len(p))]))[:, :3])
    return np.concatenate(result)


def main():
    source = ROOT / 'assetgen_output/creatures/badger_stride_v3'
    out = ROOT / 'assetgen_output/creatures/badger_ankle_control_v1'
    assert not out.exists(), 'Use a fresh proposal; do not overwrite evidence'
    recipe = yaml.safe_load((source / 'recipes/badger.yaml').read_text(encoding='utf8'))
    changed = deepcopy(recipe)
    bones = changed['anatomy']['bones']
    for suffix in ['fl', 'fr', 'bl', 'br']:
        lower = next(b for b in bones if b['name'] == 'lower_' + suffix)
        name = 'foot_' + suffix
        # A low support-articulation pivot in this stylized block paw. It is not
        # asserted to be an anatomically placed ankle or an accepted deformation.
        pivot = deepcopy(lower['pivot']); pivot[1] = .5
        bones.append({'name': name, 'parent': lower['name'], 'pivot': pivot, 'cubes': []})
        for cube in lower['cubes']:
            cube['blend'] = {'bone': name, 'axis': 'y', 'range': [0, 3],
                             'weights': [1, 0] if cube['name'].startswith('foot_') else [1, 1]}
    before, blobs, _ = inspect(source / 'models/creatures/badger')
    after, blob = export_gltf(build_model(changed), [a['name'] for a in before['animations']])
    assert len(after['skins'][0]['joints']) == 17
    assert before['animations'] == after['animations']  # accessor indices remain stable
    # Compare decoded values rather than assuming unchanged byte offsets.
    for ma, mb in zip(before['meshes'], after['meshes']):
        for pa, pb in zip(ma['primitives'], mb['primitives']):
            for name in ['POSITION', 'TEXCOORD_0']:
                assert np.array_equal(accessor(before, blobs, pa['attributes'][name]), accessor(after, [blob], pb['attributes'][name]))
            assert np.array_equal(accessor(before, blobs, pa['indices']), accessor(after, [blob], pb['indices']))
    max_error = 0.
    for clip in [None] + [a['name'] for a in before['animations']]:
        for phase in np.linspace(0, 1, 257 if clip else 1):
            error = float(np.max(np.abs(geometry(before, blobs, clip, float(phase)) - geometry(after, [blob], clip, float(phase)))))
            max_error = max(max_error, error)
    assert max_error < 1e-7, max_error
    out.mkdir(parents=True)
    shutil.copytree(source / 'models', out / 'models')
    folder = out / 'models/creatures/badger'
    (folder / 'model.gltf').write_text(json.dumps(after, indent=2) + '\n')
    (folder / 'model.bin').write_bytes(blob)
    (out / 'recipes').mkdir()
    (out / 'recipes/badger.yaml').write_text(yaml.safe_dump(changed, sort_keys=False), encoding='utf8')
    report = {'scope': __doc__, 'state': 'unapproved_identity_rig_control',
              'source_bin_sha256': sha(source / 'models/creatures/badger/model.bin'),
              'joints_before': 13, 'joints_after': 17, 'triangles': 360,
              'maximum_sampled_vertex_error_model': max_error,
              'clips': [a['name'] for a in before['animations']], 'samples_per_clip': 257,
              'bin_bytes': len(blob), 'builder_sha256': sha(Path(__file__)),
              'proposal_sha256': {p.name:sha(p) for p in [folder/'model.gltf', folder/'model.bin', out/'recipes/badger.yaml']},
              'articulation_deformation_quality': 'pending; this identity test does not exercise nonzero foot rotations'}
    (out / 'proposal_report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2), flush=True)


if __name__ == '__main__':
    main()
