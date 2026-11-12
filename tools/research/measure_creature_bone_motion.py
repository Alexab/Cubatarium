"""Run the Release native bone diagnostic with immutable input/source records."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[2]


def hashes(paths, relative):
    return {p.relative_to(relative).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(set(paths)) if p.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--models', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--clearance', action='store_true', help='Evaluate the production ground-clearance layer, without temporal takeoff blending')
    args = parser.parse_args()
    models = args.models.resolve()
    out = args.out.resolve()
    assert out.is_relative_to(ROOT / 'docs/research')
    executable = ROOT / 'build/desktop-msvc/Release/creature_bone_motion_audit.exe'
    sources = [ROOT / 'CMakeLists.txt', ROOT / 'tools/research/creature_bone_motion_audit.cpp']
    for folder in ('src/Creatures', 'src/Pose'):
        sources.extend((ROOT / folder).rglob('*.cpp'))
        sources.extend((ROOT / folder).rglob('*.h'))
    inputs = hashes(models.rglob('*'), models)
    context = {'schema': 'creature-native-bone-motion-context.v1',
               'captured_at_utc': datetime.now(timezone.utc).isoformat(),
               'models_root': str(models), 'files_sha256': inputs,
               'exe_sha256': hashlib.sha256(executable.read_bytes()).hexdigest(),
               'source_sha256': hashes(sources, ROOT), 'configuration': 'Release',
               'ground_clearance_enabled': args.clearance,
               'scope': 'Diagnostic production C++ pose evaluation, no GL/world or final contact/stride acceptance'}
    out.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(executable), str(models), str(out)] + (['--clearance'] if args.clearance else []), cwd=ROOT, check=True)
    assert inputs == hashes(models.rglob('*'), models), 'Model inputs changed during capture'
    assert context['source_sha256'] == hashes(sources, ROOT), 'Source inputs changed during capture'
    assert context['exe_sha256'] == hashlib.sha256(executable.read_bytes()).hexdigest()
    report = json.loads(out.read_text(encoding='utf8'))
    assert len(report['resources']) == 17
    summary = []
    for resource in report['resources']:
        scenarios = [row for row in resource['scenarios'] if row['ground_contact_candidate']]
        if not scenarios:
            continue
        low = min(scenarios, key=lambda s: s['lowest_y'])
        high = max(scenarios, key=lambda s: s['highest_floor_y'])
        summary.append({'id': resource['id'], 'profile': resource['profile'],
                        'lowest_y': low['lowest_y'], 'lowest_state': low['state'], 'lowest_speed_ratio': low['speed_ratio'],
                        'highest_floor_y': high['highest_floor_y'], 'highest_floor_state': high['state'],
                        'highest_floor_speed_ratio': high['speed_ratio']})
    context['input_and_executable_unchanged'] = True
    out.with_name(out.stem + '_context.json').write_text(json.dumps(context, indent=2) + '\n', encoding='utf8')
    out.with_name(out.stem + '_summary.json').write_text(json.dumps({
        'schema': 'creature-native-bone-motion-summary.v1',
        'scope': 'Ground-candidate mesh-floor extrema only, not a stance/stride verdict',
        'resources': summary}, indent=2) + '\n', encoding='utf8')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
