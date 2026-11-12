"""Verify actual cooperative user/world save evidence without granting art acceptance."""
import argparse
from datetime import datetime
import json
from pathlib import Path
from run_creature_user_world_qa import ROOT, PERSISTED, read, write, sha
from audit_creature_save_probe import canonical


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--label', required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    context_path = out / f'context_{args.label}.json'
    context = read(context_path)
    assert context['exe_sha256'] == sha(ROOT / 'bin/Cubatarium.exe')
    assert not context['qa_models_override'] and context['performance_gate'] == 'disabled'
    for name, digest in context['files_sha256'].items():
        assert sha(ROOT / 'bin/models' / name) == digest, name
    for name, digest in context['equipment_files_sha256'].items():
        assert sha(ROOT / 'bin' / name) == digest, name
    for name, digest in context['shaders_sha256'].items():
        assert sha(ROOT / name) == digest, name
    native = out / ('native_' + args.label)
    for name, digest in context['input_sha256'].items():
        assert sha(native / name) == digest
    manifest = read(native / 'manifest.json')
    assert manifest['schema'] == 'creature-user-world-native.v1' and manifest['passed']
    marker = read(native / 'fixture_marker.json')
    assert marker['world'] == context['world'] == manifest['world']
    source = read(native / 'input_users.json')
    assert set(source) == {'Username'}
    user = source['Username']
    expected_skin = user.get('selected_skin_id', user.get('selected_appearance_type', ''))
    assert expected_skin == marker['expected_skin'] == ('human_guard' if context['mode'] == 'legacy' else 'human_adventurer')
    expected_legacy = user.get('selected_appearance_type', '') if 'selected_skin_id' not in user else ''
    loaded = manifest['loaded']
    assert loaded == manifest['before_save'] == manifest['after_save']
    assert loaded['selected_skin'] == loaded['player_raw_skin'] == loaded['player_effective_skin'] == expected_skin
    assert loaded['selected_legacy_appearance'] == expected_legacy
    assert loaded['user_player_id'] == loaded['player_id'] == loaded['controlled_id'] == user['player_creature_id'] == 1
    assert loaded['is_player_character'] and loaded['player_possessed']
    assert loaded['first_person'] and loaded['fp_view_enabled']
    saved_users = read(native / 'saved_users.json')
    assert set(saved_users) == {'Username'} and manifest['saved_user_count'] == 1
    saved = saved_users['Username']
    assert saved == manifest['saved_current_user']
    assert saved['selected_skin_id'] == expected_skin and 'selected_appearance_type' not in saved
    assert saved['player_creature_id'] == 1
    assert saved['position'] == loaded['camera_position']
    assert saved['yaw'] == loaded['camera_yaw'] and saved['pitch'] == loaded['camera_pitch']
    for field in PERSISTED:
        assert user[field] == loaded['player_state'][field] == saved[field], field
    assert saved['active_bar'] == 1 and saved['active_slot'] == 4
    assert saved['hotbars'][1][4]['id'] == 'wood_sword'
    assert saved['equipped_offhand']['id'] == 'wood_torch'
    assert all(not slot['empty'] for slot in saved['equipped_armor'])
    input_creatures = read(native / 'input_creatures.json')['creatures']
    saved_creatures = read(native / 'saved_creatures.json')
    assert saved_creatures['format_version'] == 2
    source_map = {str(row['id']): canonical(row) for row in input_creatures}
    saved_map = {str(row['id']): canonical(row) for row in saved_creatures['creatures']}
    assert len(source_map) == len(saved_map) == manifest['saved_creature_count'] == 64
    assert '1' not in saved_map, 'Player character duplicated into creatures.json'
    for key, expected in source_map.items():
        assert expected == canonical(loaded['actors'][key]) == saved_map[key], key
    assert set(loaded['actors']) == set(source_map) | {'1'}
    assert {c['species'] for c in manifest['control_transitions']} == {'cow', 'badger', 'rigid_demo_walker', 'fire_spirit', 'skeleton'}
    for transition in manifest['control_transitions']:
        assert transition['transfer'] and transition['return'] and transition['effective_skin'] == ''
    assert {c['runtime_backend'] for c in manifest['control_transitions']} == {'bone_skeleton', 'gltf_skeleton', 'rigid_voxels', 'sprite'}
    assert manifest['save_phases'] == ['init', 'scan', 'chunks', 'save_metadata', 'done']
    assert manifest['save_ticks'] > 0 and manifest['save_seconds'] > 0
    metadata = read(native / 'saved_world_data.json')
    assert metadata['world_name'] == context['world']
    assert metadata['view']['projection'] == 'perspective' and metadata['view']['show_fp_wield']
    integrity = read(out / f'source_integrity_{args.label}.json')
    assert integrity['passed'] and integrity['source_world_unchanged']
    started = datetime.fromisoformat(context['captured_at_utc']).timestamp()
    for path in native.glob('*.json'):
        if not path.name.startswith('input_'):
            assert path.stat().st_mtime >= started, path
    report = {'schema': 'creature-user-world-independent-audit.v1', 'passed': True,
              'label': args.label, 'mode': context['mode'], 'restart': context['restart'],
              'exe_sha256': context['exe_sha256'], 'context_sha256': sha(context_path),
              'scope': manifest['scope'], 'loaded_user': 1, 'loaded_npcs': 64, 'control_roundtrips': 5,
              'selected_skin': expected_skin, 'legacy_migrated_to_selected_skin': bool(expected_legacy),
              'native_files_sha256': {p.name: sha(p) for p in sorted(native.glob('*.json'))}}
    write(out / f'audit_{args.label}.json', report)
    print(json.dumps({key: report[key] for key in ('passed', 'label', 'loaded_npcs', 'control_roundtrips', 'selected_skin', 'legacy_migrated_to_selected_skin')}))


if __name__ == '__main__':
    main()
