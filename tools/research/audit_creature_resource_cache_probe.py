"""Independently validate the bounded native mesh-cache regression evidence."""
import argparse
from collections import Counter
from datetime import datetime
import hashlib
import json
import math
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    return json.loads(path.read_text(encoding='utf8'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--label', default='resource_catalog')
    parser.add_argument('--native-dir', default='native_resource')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    out = args.out.resolve()
    context_path = out / f'context_{args.label}.json'
    context = read(context_path)
    native = out / args.native_dir
    probe_path = native / 'resource_cache_probe.json'
    probe = read(probe_path)
    assert context['native_resource_cache_probe'] and not context['qa_models_override']
    assert digest(repo / 'bin/Cubatarium.exe') == context['exe_sha256']
    for file, sha in context['shaders_sha256'].items():
        assert digest(repo / file) == sha, file
    root = Path(context['models_root']).resolve()
    for file, sha in context['files_sha256'].items():
        assert digest(root / file) == sha, file
    assert probe_path.stat().st_mtime >= datetime.fromisoformat(context['captured_at_utc']).timestamp()
    definitions = {p.parent.name: read(p) for p in (root / 'creatures').glob('*/creature.json')}
    backends = {sid: d['visual']['backend'] for sid, d in definitions.items()}
    assert len(backends) == 56
    assert Counter(backends.values()) == Counter(bone_skeleton=17, gltf_skeleton=35, rigid_voxels=4)
    rows = {r['id']: r for r in probe['resources']}
    assert len(rows) == len(probe['resources']) == 56 and rows.keys() == definitions.keys()
    for sid, row in rows.items():
        assert row['backend'] == backends[sid] and row['passed'] and row['live_uses_fresh_meshes']
        assert row['before_draws'] == row['fresh_draws'] == row['live_draws'] > 0
        assert row['before_hash'] == row['fresh_hash'] == row['live_hash']
    expected_ids = {sid for sid, backend in backends.items() if backend != 'rigid_voxels'}
    paths = {r['id']: r for r in probe['resource_path_cases']}
    dirs = {r['id']: r for r in probe['resource_directory_cases']}
    assert len(paths) == len(probe['resource_path_cases']) == 52 and paths.keys() == expected_ids
    assert len(dirs) == len(probe['resource_directory_cases']) == 52 and dirs.keys() == expected_ids
    fixtures = {}
    for sid in sorted(expected_ids):
        for row in (paths[sid], dirs[sid]):
            assert row['backend'] == backends[sid] and row['passed'] and row['live_uses_fresh_meshes']
            error = row['max_pose_matrix_error']
            assert math.isfinite(error) and 0 <= error <= 5e-6
            assert row['changed_content_hash'] != rows[sid]['before_hash']
        assert paths[sid]['same_species'] and paths[sid]['same_geometry_id']
        assert dirs[sid]['same_species_and_filename']
        target = Path(paths[sid]['resource']).resolve()
        assert target.parent == Path(dirs[sid]['source_directory']).resolve()
        visual = definitions[sid]['visual']
        if backends[sid] == 'bone_skeleton':
            source = root / 'creatures' / sid / visual['geometry_file']
            assert target == native / 'resource_cache_fixture' / sid / source.name
            expected = read(source)
            modified = False
            for entry in expected.values():
                if not isinstance(entry, dict) or 'bones' not in entry:
                    continue
                for bone in entry['bones']:
                    if bone.get('cubes'):
                        bone['cubes'][0]['origin'][0] += 16
                        modified = True
                        break
                if modified:
                    break
            assert modified and read(target) == expected, sid
        else:
            alternate = target.parent.name
            assert alternate != sid and backends[alternate] == 'gltf_skeleton'
            assert target == root / 'creatures' / alternate / definitions[alternate]['visual']['gltf']['model']
            assert target.name == visual['gltf']['model']
            assert str(target.relative_to(root)) in context['files_sha256']
        fixtures[str(target)] = digest(target)
    rigid = probe['rigid_pack_reload']
    sid = rigid['id']
    assert sid == 'rigid_demo_walker' and rigid['passed']
    model_name = definitions[sid]['visual']['rigid_model']
    original = read(root / 'creatures' / sid / model_name)
    expected_width = original['parts'][0]['size'][0]
    assert abs(rigid['before_width'] - expected_width) <= 1e-6
    assert abs(rigid['reloaded_width'] - expected_width * 1.25) <= 1e-6
    original['parts'][0]['size'][0] *= 1.25
    fixture = native / 'resource_cache_fixture/rigid_reload/creatures' / sid / model_name
    assert read(fixture) == original
    fixtures[str(fixture)] = digest(fixture)
    assert probe['passed']
    report = {
        'schema': 'creature-resource-cache-evidence-audit.v1', 'passed': True,
        'scope': 'Live mesh invalidation and locators, single rigid pack entry; not full appearance/pack/art/performance acceptance',
        'context': str(context_path), 'native_probe': str(probe_path),
        'context_sha256': digest(context_path), 'probe_sha256': digest(probe_path),
        'exe_sha256': context['exe_sha256'], 'current_runtime_inputs_verified': True,
        'same_root_cases': 56, 'filename_cases': 52, 'source_directory_cases': 52,
        'rigid_pack_reload_cases': 1, 'total_cases': 161,
        'max_pose_matrix_error': max(r['max_pose_matrix_error'] for r in list(paths.values()) + list(dirs.values())),
        'fixture_sha256': fixtures,
    }
    (out / 'resource_cache_audit.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(json.dumps({k: v for k, v in report.items() if k != 'fixture_sha256'}, indent=2))


if __name__ == '__main__':
    main()
