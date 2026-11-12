"""Audit production61 retained reload evidence against current payloads and GPU RGBA."""
import argparse
from collections import Counter
from datetime import datetime
import json
import math
from pathlib import Path

from PIL import Image
from audit_creature_texture_mapping_probe import digest, read, source_stems


def fnv64(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return value


def texture_path(root, key):
    skin = key.startswith('skin/')
    owner, stem = (key[5:] if skin else key).split('/')
    return root / ('skins' if skin else 'creatures') / owner / 'textures' / f'{stem}.png'


def payloads(folder, visual):
    backend = visual['backend']
    if backend == 'bone_skeleton':
        return [visual['geometry_file']]
    if backend == 'gltf_skeleton':
        model = visual['gltf']['model']
        return [model] + [b['uri'] for b in read(folder / model)['buffers']]
    return [visual['rigid_model']] if visual.get('rigid_model') else []


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--label', default='reload_catalog')
    parser.add_argument('--native-dir', default='native_catalog')
    parser.add_argument('--report', default='resource_reload_audit.json')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    out = args.out.resolve()
    context_path = out / f'context_{args.label}.json'
    context = read(context_path)
    assert context['native_resource_reload_probe'] and not context['qa_models_override']
    root = Path(context['models_root'])
    assert digest(repo / 'bin/Cubatarium.exe') == context['exe_sha256']
    for name, sha in context['files_sha256'].items():
        assert digest(root / name) == sha, name
    for name, sha in context['shaders_sha256'].items():
        assert digest(repo / name) == sha, name
    native = out / args.native_dir
    fixture = native / 'resource_reload_fixture'
    path = native / 'resource_reload_probe.json'
    probe = read(path)
    assert path.stat().st_mtime >= datetime.fromisoformat(context['captured_at_utc']).timestamp()
    definitions = {p.parent.name: read(p) for p in (root / 'creatures').glob('*/creature.json')}
    skins = {p.parent.name: read(p) for p in (root / 'skins').glob('*/skin.json')}
    assert len(definitions) == 56 and len(skins) == 5
    assert set(context['preview_species']) == definitions.keys()
    resources = {r['id']: r for r in probe['resources']}
    assert len(resources) == len(probe['resources']) == 61 and resources.keys() == definitions.keys() | skins.keys()
    used_keys = set()
    for rid, resource in resources.items():
        sid = rid if rid in definitions else skins[rid]['creature_id']
        visual = definitions[sid]['visual']
        assert resource['species'] == sid and resource['skin'] == ('' if rid in definitions else rid)
        assert resource['backend'] == visual['backend']
        assert resource['runtime_backend'] == ('sprite' if visual.get('sprite', {}).get('billboard') else visual['backend'])
        stems = source_stems(root / 'creatures' / sid, visual)
        keys = [f'{sid}/{s or "body"}' for s in stems]
        if rid in skins:
            assert visual['backend'] == 'bone_skeleton'
            keys = [f'skin/{rid}/diffuse'] * len(stems)
        assert Counter(keys) == Counter(resource['keys'])
        assert resource['draws'] == len(stems) == len(resource['geometry_hashes']) > 0
        assert sorted(resource['geometry_hashes']) == resource['geometry_hashes']
        used_keys.update(keys)
    base_pngs = {str(p.relative_to(root)).replace('\\', '/'): p
                 for name in ('creatures', 'skins') for p in (root / name).glob('*/textures/*.png')}
    expected_files = set()
    expected_copies = {}
    files = {}
    for pack in ('qa_reload_a', 'qa_reload_b'):
        pack_root = fixture / 'pack_assets/resource_packs' / pack
        assert read(pack_root / 'pack.json') == {'id': pack, 'name': pack, 'license': 'CC0-1.0',
                                                'priority': 0, 'resolution': 16, 'worldgen_role': 'secondary'}
        expected_files.add(pack_root / 'pack.json')
        for sid, definition in definitions.items():
            folder = root / 'creatures' / sid
            for name in ['creature.json'] + payloads(folder, definition['visual']):
                expected_copies[pack_root / 'creatures' / sid / name] = folder / name
        for skin in skins:
            expected_copies[pack_root / 'skins' / skin / 'skin.json'] = root / 'skins' / skin / 'skin.json'
    missing_ids = {sid for sid, d in definitions.items() if d['visual']['backend'] in ('bone_skeleton', 'gltf_skeleton')}
    assert len(missing_ids) == 52
    for sid in missing_ids:
        folder = root / 'creatures' / sid
        for name in payloads(folder, definitions[sid]['visual']):
            expected_copies[fixture / 'missing_mesh/creatures' / sid / name] = folder / name
    rigid_ids = {sid for sid, d in definitions.items() if d['visual'].get('rigid_model')}
    assert len(rigid_ids) == 3
    for sid in rigid_ids:
        for name in ('creature.json', definitions[sid]['visual']['rigid_model']):
            expected_copies[fixture / 'rigid_recovery/creatures' / sid / name] = root / 'creatures' / sid / name
    copies = {Path(row['target']): Path(row['source']) for row in probe['source_copies']}
    assert copies == expected_copies and len(copies) == len(probe['source_copies']) == 395
    for target, source in copies.items():
        assert digest(target) == digest(source) == context['files_sha256'][str(source.relative_to(root))]
        expected_files.add(target)
    synthetic_roots = [(fixture / 'texture_root_b', 1),
                       (fixture / 'pack_assets/resource_packs/qa_reload_a', 2),
                       (fixture / 'pack_assets/resource_packs/qa_reload_b', 3)]
    for target_root, variant in synthetic_roots:
        for name in base_pngs:
            source = root / name
            parts = Path(name).parts
            key = ('skin/' if parts[0] == 'skins' else '') + parts[1] + '/' + source.stem
            target = texture_path(target_root, key)
            expected_files.add(target)
            h = fnv64(key.encode())
            color = (23 + (h + variant * 67) % 211, 19 + ((h >> 8) + variant * 41) % 223,
                     29 + ((h >> 16) + variant * 83) % 199, 255)
            with Image.open(target) as image:
                assert image.size == (4, 4) and image.convert('RGBA').tobytes() == bytes(color) * 16
    expected_stages = ['baseline', 'missing_textures', 'merge_recovery', 'texture_root_swap', 'texture_root_return',
                       'pack_selection_ab', 'pack_selection_ba', 'pack_tier', 'pack_removed', 'pack_repeat', 'pack_final_base']
    assert [s['name'] for s in probe['texture_stages']] == expected_stages
    gpu_count = 0
    for stage in probe['texture_stages']:
        name = stage['name']
        missing = name == 'missing_textures'
        assert stage['missing'] == missing and stage['passed']
        winner = {'pack_selection_ab': 'qa_reload_a', 'pack_selection_ba': 'qa_reload_b',
                  'pack_tier': 'qa_reload_b', 'pack_repeat': 'qa_reload_a'}.get(name, '')
        source_root = fixture / 'texture_root_b' if name == 'texture_root_swap' else fixture / 'pack_assets/resource_packs' / winner if winner else root
        if name.startswith('pack_'):
            primary, secondary = (['qa_reload_a'], ['qa_reload_b']) if name == 'pack_tier' else ([], ['qa_reload_a', 'qa_reload_b']) if name in ('pack_selection_ab', 'pack_repeat') else ([], ['qa_reload_b', 'qa_reload_a']) if name == 'pack_selection_ba' else ([], [])
            assert stage['selection_primary'] == primary and stage['selection_secondary'] == secondary
            order = []
            for tier, selected in enumerate((primary, secondary)):
                order += [{'id': pid, 'priority': tier * 1000 + (len(selected) - 1 - i) * 10, 'selection_index': i}
                          for i, pid in enumerate(selected)]
            assert stage['resolved_order'] == sorted(order, key=lambda p: p['priority'])
            assert stage['winner'] == winner and stage['source_directories_correct'] and stage['catalog_revision_changed']
        gpu = {g['key']: g for g in stage['gpu_textures']}
        assert len(gpu) == len(stage['gpu_textures']) and gpu.keys() == (set() if missing else used_keys)
        assert len({g['texture'] for g in gpu.values()}) == len(gpu)
        for key, sample in gpu.items():
            source = texture_path(source_root, key)
            assert Path(sample['source']) == source and sample['texture'] > 0
            with Image.open(source) as image:
                assert [sample['width'], sample['height']] == list(image.size)
                assert sample['source_rgba_fnv64'] == sample['gpu_rgba_fnv64'] == fnv64(image.convert('RGBA').tobytes())
            assert sample['passed']
            gpu_count += 1
        rows = {r['id']: r for r in stage['resources']}
        assert len(rows) == len(stage['resources']) == 61 and rows.keys() == resources.keys()
        for rid, row in rows.items():
            resource = resources[rid]
            directory = fixture / 'pack_assets/resource_packs' / winner / 'creatures' / resource['species'] if winner else root / 'creatures' / resource['species']
            assert Path(row['definition_source_directory']) == directory
            if missing:
                assert row['draws'] == 0 and row['textures'] == row['expected_textures'] == row['geometry_hashes'] == []
            else:
                assert row['draws'] == resource['draws']
                assert row['textures'] == row['expected_textures'] == sorted(gpu[k]['texture'] for k in resource['keys'])
                assert row['geometry_hashes'] == resource['geometry_hashes']
            assert row['passed']
        if missing:
            assert stage['old_gpu_names_deleted']
    expected_mesh = ['missing_initial', 'files_appeared_without_invalidation', 'explicit_clear_recovery', 'root_missing', 'root_return']
    assert [s['name'] for s in probe['mesh_stages']] == expected_mesh
    for stage in probe['mesh_stages']:
        name = stage['name']
        absent = name in ('missing_initial', 'files_appeared_without_invalidation', 'root_missing')
        directory = root / 'creatures' if name == 'root_return' else fixture / 'empty_mesh_root' if name == 'root_missing' else fixture / 'missing_mesh/creatures'
        assert Path(stage['bone_root']) == Path(stage['gltf_root']) == directory
        assert stage['expected_absent'] == absent and stage['passed']
        rows = {r['id']: r for r in stage['resources']}
        assert len(rows) == len(stage['resources']) == 52 and rows.keys() == missing_ids
        for sid, row in rows.items():
            assert row['definition_source_directory'] == '' and row['passed'] and row['fresh_meshes']
            assert row['live_hash'] == row['fresh_hash']
            assert row['draws'] == row['fresh_draws'] == (0 if absent else resources[sid]['draws'])
    rigid = {r['id']: r for r in probe['rigid_recovery']}
    assert len(rigid) == len(probe['rigid_recovery']) == 3 and rigid.keys() == rigid_ids
    for sid, row in rigid.items():
        original = read(root / 'creatures' / sid / definitions[sid]['visual']['rigid_model'])
        target = fixture / 'rigid_changes' / sid / 'changed_model.json'
        changed = read(target)
        width = original['parts'][0]['size'][0]
        assert len(original['parts']) > 1 and math.isclose(row['before_width'], width, abs_tol=1e-6)
        assert math.isclose(row['changed_width'], width * 1.125, abs_tol=1e-6)
        assert math.isclose(changed['parts'][0]['size'][0], row['changed_width'], abs_tol=1e-6)
        changed['parts'][0]['size'][0] = width
        assert changed == original and Path(row['changed_fixture']) == target
        expected_files.add(target)
        assert row['placeholder_draws'] == 1 and row['placeholder_parts_empty']
        assert row['original_hash'] == row['recovered_hash'] == row['restored_hash']
        assert row['changed_hash'] == row['fresh_hash'] != row['original_hash']
        assert row['changed_geometry'] and row['passed']
    clear = probe['empty_catalog']
    assert clear['count'] == 0 and clear['revision_after'] == clear['revision_before'] + 1 and clear['passed']
    actual_files = {p for p in fixture.rglob('*') if p.is_file()}
    assert actual_files == expected_files, (actual_files - expected_files, expected_files - actual_files)
    for source in sorted(expected_files):
        files[str(source.relative_to(native))] = digest(source)
    assert probe['passed']
    report = {'schema': 'creature-resource-reload-evidence-audit.v1', 'passed': True,
              'scope': 'Local actual loader/resolver/merge plus retained visual submissions and GPU RGBA; not world bootstrap UI/FP/save, all-motion art, clean package or performance acceptance',
              'context': str(context_path), 'context_sha256': digest(context_path), 'probe_sha256': digest(path),
              'exe_sha256': context['exe_sha256'], 'current_runtime_inputs_verified': True,
              'resources': 61, 'texture_stages': 11, 'texture_cases': 671, 'gpu_readbacks': gpu_count,
              'negative_mesh_cases': 260, 'rigid_recovery_cases': 3, 'empty_catalog_cases': 1,
              'native_cases': 935, 'all_positive_stage_geometry_unchanged': True, 'source_copies': 395,
              'fixture_sha256': files}
    target = out / args.report
    target.write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(json.dumps({k: v for k, v in report.items() if k != 'fixture_sha256'}, indent=2))


if __name__ == '__main__':
    main()
