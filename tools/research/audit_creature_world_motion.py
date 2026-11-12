"""Verify actual derived fluid locomotion and XZ travel, never art acceptance."""
import argparse
from collections import Counter
import json
from pathlib import Path
import numpy as np
from audit_creature_motion_v5 import ROOT, sha


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--label', required=True)
    args = parser.parse_args(); out = args.out.resolve()
    context = read(out / ('context_' + args.label + '.json'))
    process = read(out / ('report_' + args.label + '.json'))
    assert process['process_success'] and process['process_rc'] == 0 and not process['hang_killed']
    assert context['exe_sha256'] == sha(ROOT / 'bin/Cubatarium.exe')
    driver = context['world_motion']
    assert driver in ('water_pause', 'water_swim_pause') and len(context['world_species']) == 1
    for field, root in [('files_sha256', Path(context['models_root'])), ('shaders_sha256', ROOT)]:
        for name, digest in context[field].items(): assert sha(root / name) == digest, name
    expected = context['world_motion_expected_fluid_state'].lower()
    assert expected in ('swim', 'tread')
    log = out / ('world_motion_' + args.label + '.jsonl')
    records = [json.loads(line) for line in log.read_text().splitlines() if line]
    assert records and {r['species'] for r in records} == set(context['world_species'])
    if driver == 'water_swim_pause':
        assert expected == 'swim'
        assert all(r['driver'] == driver and r['depth_target_y'] is not None for r in records)
        assert any(r['requested_jump'] for r in records)
        target = records[0]['depth_target_y']
        assert all(abs(r['depth_target_y'] - target) < 1e-5 for r in records)
    assert not any(r['frozen'] or r['body_blocked'] for r in records)
    assert records[-1]['elapsed'] > 4.75
    assert any(not r['requested_move'] and r['in_fluid'] for r in records)
    selected = [r for r in records if r['state'].lower() == expected and r['in_fluid'] and r['requested_move']]
    assert len(selected) >= 10
    if expected == 'tread': assert all(r['on_fluid_bottom'] for r in selected)
    else: assert all(not r['on_fluid_bottom'] for r in selected)
    travel, directions = 0., Counter()
    for before, after in zip(records, records[1:]):
        assert after['elapsed'] >= before['elapsed']
        if before['id'] != after['id'] or after['state'].lower() != expected or not after['requested_move']: continue
        delta = np.array(after['position']) - before['position']
        distance = float(np.linalg.norm(delta[[0, 2]]))
        travel += distance
        if distance > 1e-5: directions[str(after['requested_direction'])] += 1
    assert travel > .5 and directions['1.0'] > 0 and directions['-1.0'] > 0
    report = {'passed': True, 'scope': 'One ordinary-input actual world fluid route; derived state and XZ travel, not all terrain/sliding/world attack/AI/art/performance acceptance',
              'exe_sha256': context['exe_sha256'], 'qa_models_override': context['qa_models_override'],
              'expected_state': expected, 'world_samples': len(records), 'sim_seconds': records[-1]['elapsed'],
              'driver': driver, 'requested_jump_samples': sum(bool(r.get('requested_jump')) for r in records),
              'state_counts': dict(Counter(r['state'] for r in records)), 'matching_move_samples': len(selected),
              'xz_travel_in_expected_state': travel, 'move_direction_samples': dict(directions), 'frozen_or_solid_overlap': False,
              'inputs_sha256': {p.name: sha(p) for p in [log, out / ('context_' + args.label + '.json'), out / ('report_' + args.label + '.json')]}}
    (out / 'world_motion_audit.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__': main()
