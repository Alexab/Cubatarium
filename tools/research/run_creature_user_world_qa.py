"""Run ordinary cooperative world/user load and save in a marked disposable clone."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
PERSISTED = ('storage', 'hotbars', 'active_bar', 'active_slot', 'equipped_armor',
             'equipped_offhand', 'equipped_armor_groups', 'vitals', 'attributes')


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def write(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf8')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inventory(folder):
    return {p.relative_to(folder).as_posix(): sha(p) for p in sorted(folder.rglob('*')) if p.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--label', required=True)
    parser.add_argument('--world', required=True)
    parser.add_argument('--source-world', default='World_254')
    parser.add_argument('--mode', choices=('legacy', 'selected'), required=True)
    parser.add_argument('--restart', action='store_true')
    args = parser.parse_args()
    for name in (args.label, args.world, args.source_world):
        if Path(name).name != name or name in ('.', '..') or '/' in name or '\\' in name:
            raise ValueError('Labels/worlds must be plain leaf names')
    if not args.world.startswith('CreatureQaUser_'):
        raise ValueError('Only disposable CreatureQaUser_ worlds are authorized')
    out = args.out.resolve()
    assert out.is_relative_to((ROOT / 'docs/research').resolve())
    native = out / ('native_' + args.label)
    context_file = out / f'context_{args.label}.json'
    assert not native.exists() and not context_file.exists(), 'Evidence labels are immutable'
    worlds = (ROOT / 'bin/worlds').resolve()
    source = (worlds / args.source_world).resolve()
    world = (worlds / args.world).resolve()
    assert source.parent == world.parent == worlds and source != world
    source_before = inventory(source)
    assert source_before
    skin = 'human_guard' if args.mode == 'legacy' else 'human_adventurer'
    marker = {'schema': 'creature-user-world-fixture.v1', 'world': args.world,
              'mode': args.mode, 'expected_skin': skin, 'user': 'Username'}
    fixtures = ROOT / 'docs/research/creature-r6-v2/save-roundtrip/native_first/save'
    if args.restart:
        assert read(world / 'creature_user_qa_fixture.json') == marker
    else:
        assert not world.exists(), 'Fresh worlds must not overwrite any existing world'
        data = read(source / 'world_data.json')
        data.update(world_name=args.world, game_mode='creative', hardcore_dead=False)
        data['view'] = dict(data.get('view', {}), projection='perspective', show_fp_wield=True)
        data['environment'] = dict(data.get('environment', {}), time_of_day=.35, time_frozen=True,
                                   weather='clear', weather_target='clear')
        expected = read(fixtures / 'v2_expected.json')
        sample = next(row for row in expected.values() if row['type'] == 'human' and row['skin_id'] == skin)
        user = {key: sample[key] for key in PERSISTED}
        user['hotbars'][0][0] = {'empty': False, 'kind': 'item', 'id': 'wood_torch',
                                  'count': 1, 'wear': .25, 'broken': False}
        user.update(position=data['spawn_point'], yaw=-90., pitch=-10., player_creature_id=1)
        user['selected_appearance_type' if args.mode == 'legacy' else 'selected_skin_id'] = skin
        world.mkdir()
        shutil.copytree(source / 'chunks', world / 'chunks')
        for name in ('chunks.json', 'column_light.json'):
            if (source / name).is_file():
                shutil.copy2(source / name, world / name)
        write(world / 'world_data.json', data)
        write(world / 'users.json', {'Username': user})
        shutil.copy2(fixtures / 'v2_saved_once.json', world / 'creatures.json')
        write(world / 'creature_user_qa_fixture.json', marker)
    native.mkdir(parents=True)
    for name in ('users', 'creatures', 'world_data'):
        shutil.copy2(world / f'{name}.json', native / f'input_{name}.json')
    model_hashes = {}
    for kind in ('creatures', 'skins'):
        for path in sorted((ROOT / 'bin/models' / kind).rglob('*')):
            if path.is_file():
                model_hashes[path.relative_to(ROOT / 'bin/models').as_posix()] = sha(path)
    equipment = read(ROOT / 'docs/research/creature-r6-v2/save-roundtrip/context_save_first.json')['equipment_files_sha256']
    assert all(sha(ROOT / 'bin' / name) == digest for name, digest in equipment.items())
    context = {'schema': 'creature-user-world-context.v1', 'captured_at_utc': datetime.now(timezone.utc).isoformat(),
               'world': args.world, 'label': args.label, 'mode': args.mode, 'restart': args.restart,
               'exe_sha256': sha(ROOT / 'bin/Cubatarium.exe'), 'files_sha256': model_hashes,
               'equipment_files_sha256': equipment, 'source_world_files_sha256': source_before,
               'shaders_sha256': {p.relative_to(ROOT).as_posix(): sha(p) for p in sorted((ROOT / 'bin/shaders').glob('*.glsl'))},
               'input_sha256': {p.name: sha(p) for p in sorted(native.glob('input_*.json'))},
               'performance_gate': 'disabled', 'qa_models_override': False,
               'uses_ordinary_world_load': True, 'uses_ordinary_fp_renderer': True}
    write(context_file, context)
    env = {key: value for key, value in os.environ.items()
           if not key.startswith('CUBA_FLIGHT_QA_') and key != 'CUBA_CREATURE_QA_MODELS'}
    env.update(CUBA_FLIGHT_QA_WORLD_USER=str(native),
               CUBA_FLIGHT_CAPTURE_DIR=str(out / ('runtime_' + args.label)),
               CUBA_FLIGHT_CAPTURE_INTERVAL_SEC='3')
    started = time.time()
    subprocess.run([sys.executable, '-X', 'utf8', str(ROOT / 'tools/flight_sim_run.py'),
                    '--world', args.world, '--require-existing-world', '--seconds', '8', '--no-fly',
                    '--process-timeout', '300', '--report', str(out / f'report_{args.label}.json')],
                   cwd=ROOT, env=env, check=True)
    manifest_file = native / 'manifest.json'
    assert manifest_file.stat().st_mtime >= started
    manifest = read(manifest_file)
    assert manifest['passed'] and manifest['save_phases'][-1] == 'done'
    assert len(manifest['control_transitions']) == 5
    assert inventory(source) == source_before, 'Original source world changed'
    write(out / f'source_integrity_{args.label}.json', {'passed': True, 'source_world_unchanged': True,
                                                     'source_file_count': len(source_before)})
    print(json.dumps({'passed': True, 'label': args.label, 'user_skin': manifest['loaded']['selected_skin'],
                      'saved_creatures': manifest['saved_creature_count'], 'save_phases': manifest['save_phases']}))


if __name__ == '__main__':
    main()
