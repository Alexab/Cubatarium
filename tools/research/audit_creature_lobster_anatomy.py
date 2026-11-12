"""Independent own-rig refit/contact checks; never grants art acceptance."""
import argparse
from copy import deepcopy
import json
from pathlib import Path
import sys
import numpy as np
import yaml
from review_creature_redesign import ROOT, accessor, authored_vertex_owners, sample_nodes, world_matrices
from audit_creature_motion_v5 import inspect, sha
from measure_creature_gltf_motion import measure

sys.path.insert(0, str(ROOT / 'tools'))
from creaturegen.anatomy import build_model
from creaturegen.export_gltf import export_gltf


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qa-name', required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--before-root', type=Path, default=ROOT)
    args = parser.parse_args(); out = args.out.resolve(); out.mkdir(parents=True, exist_ok=True)
    qa = ROOT / 'assetgen_output/creatures' / args.qa_name
    old_folder = args.before_root / 'models/creatures/lobster'
    folder = qa / 'models/creatures/lobster'
    source = yaml.load((args.before_root / 'tools/creature_recipes/lobster.yaml').read_text(encoding='utf-8'), Loader=yaml.CSafeLoader)
    recipe = yaml.load((qa / 'recipes/lobster.yaml').read_text(encoding='utf-8'), Loader=yaml.CSafeLoader)
    old_bones = {b['name']: b for b in source['anatomy']['bones']}
    bones = {b['name']: b for b in recipe['anatomy']['bones']}
    assert list(old_bones) == list(bones) and len(bones) == 51
    assert [b.get('parent') for b in old_bones.values()] == [b.get('parent') for b in bones.values()]
    g, blob, before_stats = inspect(old_folder); h, data, stats = inspect(folder)
    assert len(g['nodes']) == len(h['nodes']) and g['skins'] == h['skins']
    assert read(old_folder / 'creature.json') == read(folder / 'creature.json')
    changed = []
    for a, b in zip(g['nodes'], h['nodes']):
        assert {k: v for k, v in a.items() if k != 'translation'} == {k: v for k, v in b.items() if k != 'translation'}
        if a != b: changed.append(a['name'])
    expected = {name for name in bones if bones[name]['pivot'] != old_bones[name]['pivot']}
    assert set(changed) == expected and len(changed) in (16, 18)
    nodes = {n['name']: i for i, n in enumerate(h['nodes']) if n.get('name') in bones}
    skin = h['skins'][0]; inverse = accessor(h, data, skin['inverseBindMatrices']).reshape(-1, 4, 4).transpose(0, 2, 1)
    world = world_matrices(h['nodes'])
    bind_error = max(float(np.max(np.abs(world[n] @ m - np.eye(4)))) for n, m in zip(skin['joints'], inverse))
    assert bind_error < 1e-6
    for name in changed:
        parent = bones[name].get('parent')
        new_local = (np.array(bones[name]['pivot']) - (np.array(bones[parent]['pivot']) if parent else 0)) / 16
        assert np.max(np.abs(np.array(h['nodes'][nodes[name]]['translation']) - new_local)) < 1e-7
    rotations, translation_changes = 0, []
    assert len(g['animations']) == len(h['animations'])
    for a, b in zip(g['animations'], h['animations']):
        assert a['name'] == b['name'] and a['channels'] == b['channels'] and len(a['samplers']) == len(b['samplers'])
        for x, y in zip(a['channels'], b['channels']):
            s, t = a['samplers'][x['sampler']], b['samplers'][y['sampler']]
            assert s.get('interpolation') == t.get('interpolation')
            assert np.array_equal(accessor(g, blob, s['input']), accessor(h, data, t['input']))
            same = np.array_equal(accessor(g, blob, s['output']), accessor(h, data, t['output']))
            name = g['nodes'][x['target']['node']]['name']
            if x['target']['path'] == 'rotation': assert same; rotations += 1
            elif not same:
                assert name.startswith('foot_') or (name == 'body' and a['name'] == 'die')
                translation_changes.append({'clip': a['name'], 'bone': name})
    model = build_model(recipe)
    contact = measure(folder, model, 257)
    assert all(c['min_geometry_y_blocks'] >= -.001 for c in contact.values())
    primitive = h['meshes'][0]['primitives'][0]; attrs = primitive['attributes']
    p = accessor(h, data, attrs['POSITION']); joint = accessor(h, data, attrs['JOINTS_0']).astype(int)
    weight = accessor(h, data, attrs['WEIGHTS_0']); hom = np.column_stack([p, np.ones(len(p))])
    owners = authored_vertex_owners(model, h, data)
    bone_order = {b.name: i for i, b in enumerate(model.bones)}
    sections = []
    for side in ('left', 'right'):
        for i in range(4):
            for part, child in [('upper', 'lower'), ('lower', 'foot')]:
                name, target = f'{part}_{side}{i}', f'{child}_{side}{i}'
                a = np.array(bones[name]['pivot'])[[0, 2]] / 16
                b = np.array(bones[target]['pivot'])[[0, 2]] / 16
                d = b - a; t = ((p[:, [0, 2]] - a) @ d) / np.dot(d, d)
                mask = (owners == bone_order[name]) & (np.abs(t - 1) < 1e-6)
                assert mask.sum() == 12, (name, int(mask.sum()))
                sections.append((name, target, mask))
        name = f'moving_finger_{side}'; end_z = bones[name]['pivot'][2] / 16
        mask = (owners == bone_order[name]) & (np.abs(p[:, 2] - end_z) < 1e-6)
        assert mask.sum() == 12
        sections.append((name, name, mask))
    old_nodes = {n['name']: i for i, n in enumerate(g['nodes']) if n.get('name') in bones}
    foot_y_error, root_y_error, section_error = 0., 0., 0.
    scale = read(folder / 'creature.json')['visual']['gltf']['model_scale']
    for clip in ('idle', 'walk', 'punch'):
        for phase in np.linspace(0, 1, 257):
            old_world = world_matrices(sample_nodes(g, blob, clip, float(phase)))
            world = world_matrices(sample_nodes(h, data, clip, float(phase)))
            for name in bones:
                if name.startswith('foot_'):
                    foot_y_error = max(foot_y_error, abs(float(world[nodes[name]][1, 3] - old_world[old_nodes[name]][1, 3])) * scale)
            root_y_error = max(root_y_error, abs(float(world[nodes['body']][1, 3] - old_world[old_nodes['body']][1, 3])) * scale)
            palette = np.array([world[n] @ m for n, m in zip(skin['joints'], inverse)])
            matrices = (palette[joint] * weight[:, :, None, None]).sum(axis=1)
            points = np.einsum('nij,nj->ni', matrices, hom)[:, :3]
            for name, target, mask in sections:
                error = float(np.linalg.norm(points[mask].mean(axis=0) - world[nodes[target]][:3, 3])) * scale
                section_error = max(section_error, error)
    # Bind compensation is nonlinear in the knee angle; keeping the original
    # LINEAR translation key times gives tiny between-key interpolation error.
    # 1e-4 block is <0.004 authored texel and ten times tighter than the floor
    # gate. It still rejects the unintended v2 lift change of0.016875 block.
    assert foot_y_error < 1e-4 and root_y_error < 1e-6 and section_error < 1e-6, {
        'foot_y_error': foot_y_error, 'body_root_y_error': root_y_error, 'attachment_center_error': section_error}
    # Fault injection: retaining the old clip-local translations after a bind
    # edit can make contact auto-correction hide the error by lowering the body.
    bad = deepcopy(recipe)
    old_tracks = {(clip, t['bone']): t for clip, c in source['motion']['clips'].items() for t in c['tracks'] if t['path'] == 'translation'}
    for clip, c in bad['motion']['clips'].items():
        for t in c['tracks']:
            if t['path'] == 'translation' and t['bone'].startswith('foot_'):
                t['values'] = deepcopy(old_tracks[(clip, t['bone'])]['values'])
    bad_g, bad_bin = export_gltf(build_model(bad), [a['name'] for a in h['animations']])
    bad_error = 0.
    for phase in np.linspace(0, 1, 65):
        good_world = world_matrices(sample_nodes(h, data, 'walk', float(phase)))
        bad_world = world_matrices(sample_nodes(bad_g, [bad_bin], 'walk', float(phase)))
        for name in ('body', *[n for n in bones if n.startswith('foot_')]):
            bad_error = max(bad_error, abs(float(good_world[nodes[name]][1, 3] - bad_world[nodes[name]][1, 3])) * scale)
    assert bad_error > .05, 'fault injection did not expose a meaningful rig/contact regression'
    report = {'passed': True, 'scope': 'Own refit invariants, sampled floor/joint-centre attachment and translation fault injection; not surface watertightness, world terrain, stride or artistic acceptance',
              'joints': 51, 'bind_nodes_refit': changed, 'angular_channels_byte_exact': rotations,
              'all_names_parents_channel_targets_and_key_times_preserved': True, 'bind_identity_max_error': bind_error,
              'changed_translation_channels': translation_changes, 'foot_joint_y_max_error_blocks': foot_y_error,
              'body_root_y_max_error_blocks': root_y_error, 'attachment_sections': len(sections), 'attachment_center_max_error_blocks': section_error,
              'fault_old_translation_regression_blocks': bad_error, 'contact_257': contact,
              'before_geometry': before_stats, 'candidate_geometry': stats,
              'source_recipe_sha256': sha(args.before_root / 'tools/creature_recipes/lobster.yaml'),
              'candidate_recipe_sha256': sha(qa / 'recipes/lobster.yaml'),
              'payload_sha256': {str(p.relative_to(folder)): sha(p) for p in folder.rglob('*') if p.is_file()}}
    (out / 'rig_contact_audit.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('Lobster refit:', rotations, 'angular channels exact;', len(sections), 'attachments; foot Y error', foot_y_error, 'fault', bad_error)


if __name__ == '__main__': main()
