"""Independently check native entity persistence evidence, never art acceptance."""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def entities(root):
    rows = root['creatures']
    result = {str(row['id']): row for row in rows}
    assert len(result) == len(rows) == 64
    return result


def canonical(row):
    result = dict(row)
    result.setdefault('skin_id', '')
    result.setdefault('footprint_swapped', False)
    result.setdefault('spawn_source', 'legacy_save')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--label', required=True)
    parser.add_argument('--native-dir', required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    context_path = out / f'context_{args.label}.json'
    context = read(context_path)
    assert context['native_save_probe'] and not context['qa_models_override']
    assert context['performance_gate'] == 'disabled'
    assert sha(ROOT / 'bin/Cubatarium.exe') == context['exe_sha256']
    for name, digest in context['files_sha256'].items():
        assert sha(Path(context['models_root']) / name) == digest, name
    for name, digest in context['equipment_files_sha256'].items():
        assert sha(ROOT / 'bin' / name) == digest, name
    for name, digest in context['shaders_sha256'].items():
        assert sha(ROOT / name) == digest, name
    folder = out / args.native_dir / 'save'
    manifest = read(folder / 'manifest.json')
    assert manifest['schema'] == 'creature-save-roundtrip.v1'
    assert manifest['passed'] and manifest['controlled_actor_unchanged'] and manifest['world_actors_unchanged']
    assert manifest['world_actor_count'] > 0
    species = {p.parent.name for p in (ROOT / 'models/creatures').glob('*/creature.json')}
    skins = {p.parent.name: read(p) for p in (ROOT / 'models/skins').glob('*/skin.json')}
    assert len(species) == 56 and len(skins) == 5
    assert all(f'creatures/{sid}/creature.json' in {name.replace('\\', '/') for name in context['files_sha256']} for sid in species)
    assert all(f'skins/{sid}/skin.json' in {name.replace('\\', '/') for name in context['files_sha256']} for sid in skins)
    resources = {sid: (sid, '') for sid in species}
    resources.update({sid: (skin['creature_id'], sid) for sid, skin in skins.items()})
    resources.update(legacy_scout=('sheep', 'sheep_wool_golden'),
                     legacy_drifter=('wolf', 'wolf_snow'), legacy_brute=('sand_monster', ''))
    aliases = {'scout': 'sheep', 'drifter': 'wolf', 'brute': 'sand_monster'}
    skin_aliases = {'scout_golden': 'sheep_wool_golden', 'drifter_ice': 'wolf_snow', 'brute_rust': ''}
    assert {case['kind'] for case in manifest['cases']} == {'legacy', 'v2'}
    checked = []
    for case in manifest['cases']:
        kind = case['kind']
        source = read(folder / f'{kind}_input.json')
        once = read(folder / f'{kind}_saved_once.json')
        twice = read(folder / f'{kind}_saved_twice.json')
        expected_file = read(folder / f'{kind}_expected.json')
        assert (source.get('format_version', 0) == 0) == (kind == 'legacy')
        assert once['format_version'] == twice['format_version'] == 2
        inputs, first, second = map(entities, (source, once, twice))
        assert first == second and case['canonical_save_stable']
        assert case['first_count'] == case['second_count'] == 64
        assert {row['resource'] for row in case['resources']} == set(resources)
        assert len(case['resources']) == 64
        for row in case['resources']:
            key = str(row['id'])
            expected = canonical(inputs[key])
            expected['type'] = aliases.get(expected['type'], expected['type'])
            expected['skin_id'] = skin_aliases.get(expected['skin_id'], expected['skin_id'])
            assert expected['spawn_source'] == ('legacy_save' if kind == 'legacy' else 'qa')
            assert (expected['type'], expected['skin_id']) == resources[row['resource']]
            assert expected_file[key] == row['expected_state'] == row['loaded_state'] == row['reloaded_state'] == expected
            assert canonical(first[key]) == canonical(second[key]) == expected
            assert expected['body_origin'][1:] == [512.0, 10000.0]
            assert expected['storage'] == {'wood_sword': 3, 'wood_torch': 7}
            assert len(expected['hotbars']) == 2 and len(expected['hotbars'][1]) == 10
            assert expected['active_bar'] == 1 and expected['active_slot'] == 4
            held = expected['hotbars'][1][4]
            assert held == {'empty': False, 'kind': 'item', 'id': 'wood_sword', 'count': 1, 'wear': .375, 'broken': False}
            geared = expected['type'] in {'human', 'bot', 'zombie', 'skeleton'}
            for i, slot in enumerate(('head', 'chest', 'arms', 'hands', 'legs', 'feet')):
                armor = expected['equipped_armor'][i]
                assert armor['empty'] != geared
                if geared:
                    assert armor['id'] == 'copper_' + slot and armor['wear'] == (i + 1) * .125
            assert expected['equipped_offhand']['empty'] != geared
            if geared:
                assert expected['equipped_offhand']['id'] == 'wood_torch'
                assert expected['equipped_offhand']['wear'] == .25
            vitals = expected['vitals']
            for current, maximum, ratio in (('health', 'max_health', .75), ('satiety', 'max_satiety', .625),
                                             ('thirst', 'max_thirst', .5), ('fatigue', 'max_fatigue', .25),
                                             ('breath', 'max_breath', .875)):
                assert abs(vitals[current] - vitals[maximum] * ratio) <= 1e-5
            draw = row['expected_draw']
            assert row['loaded_draw'] == row['reloaded_draw'] == draw
            assert draw['draws'] > 0 and len(draw['geometry_hashes']) == len(draw['textures']) == draw['draws']
            assert all(draw['textures']) and draw['pose_values']
            assert draw['effective_skin'] == expected['skin_id']
            definition = read(ROOT / 'models/creatures' / expected['type'] / 'creature.json')
            bounds = definition['bounds']['rest'][:]
            if expected['footprint_swapped']:
                bounds[0], bounds[2] = bounds[2], bounds[0]
            assert len(draw['collision_size']) == 3
            assert max(abs(a - b) for a, b in zip(draw['collision_size'], bounds)) < 1e-5
            assert max(abs(a - b) for a, b in zip(draw['eye_offset'], [0., definition['eye_height'], 0.])) < 1e-5
            assert draw['backend'] in {'bone_skeleton', 'gltf_skeleton', 'rigid_voxels', 'sprite'}
            assert draw['backend'] == ('sprite' if definition['visual'].get('sprite', {}).get('billboard') else definition['visual']['backend'])
            checked.append({'kind': kind, 'id': row['id'], 'resource': row['resource'],
                            'backend': draw['backend'], 'draws': draw['draws'], 'passed': True})
        assert case['passed']
    assert len(checked) == 128
    files = {str(path.relative_to(out)): sha(path) for path in sorted(folder.glob('*.json'))}
    started = datetime.fromisoformat(context['captured_at_utc']).timestamp()
    assert all(path.stat().st_mtime >= started for path in folder.glob('*.json'))
    assert not list(folder.glob('*.tmp')), 'Atomic-save temporary file remains'
    report = {'schema': 'creature-save-independent-audit.v1', 'passed': True,
              'scope': manifest['scope'], 'exe_sha256': context['exe_sha256'],
              'context_sha256': sha(context_path), 'native_files_sha256': files,
              'resources_checked': len(checked), 'actual_loads': 256,
              'world_actors_unchanged': True, 'cases': checked}
    (out / f'audit_{args.label}.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(json.dumps({key: report[key] for key in ('passed', 'resources_checked', 'actual_loads', 'exe_sha256')}))


if __name__ == '__main__':
    main()
