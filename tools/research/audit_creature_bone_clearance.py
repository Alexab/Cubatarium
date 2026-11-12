"""Check native before/after floor envelopes without granting full R6 acceptance."""
import argparse
import json
from pathlib import Path


def near(a, b):
    return abs(a-b) < 3e-7


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    out = args.out
    historical = json.loads((out/'current.json').read_text())
    raw = json.loads((out/'current_raw_control.json').read_text())
    corrected = json.loads((out/'current_clearance.json').read_text())
    contexts = [json.loads((out/(name+'_context.json')).read_text())
                for name in ('current_raw_control', 'current_clearance')]
    assert contexts[0]['exe_sha256'] == contexts[1]['exe_sha256']
    assert contexts[0]['files_sha256'] == contexts[1]['files_sha256']
    assert contexts[0]['source_sha256'] == contexts[1]['source_sha256']
    assert not raw['ground_clearance_enabled'] and corrected['ground_clearance_enabled']
    assert len(historical['resources']) == len(raw['resources']) == len(corrected['resources']) == 17
    previous = {r['id']: r for r in historical['resources']}
    control = {r['id']: r for r in raw['resources']}
    rows = []
    exempt = {'rabbit', 'aerial', 'aquatic', 'dolphin', 'squid', 'serpentine'}
    for resource in corrected['resources']:
        rid = resource['id']
        raw_resource = control[rid]
        old = {(s['state'], s['speed_ratio']): s for s in previous[rid]['scenarios']}
        before = {(s['state'], s['speed_ratio']): s for s in raw_resource['scenarios']}
        fixes = 0
        for after in resource['scenarios']:
            key = after['state'], after['speed_ratio']
            ref = before[key]
            # Confirm adding the diagnostic flag did not silently change the
            # underlying pose engine. Old reports predate greatest_root_lift.
            assert {k: v for k, v in ref.items() if k != 'greatest_root_lift'} == old[key], (rid, key)
            applied = ref['ground_contact_candidate'] and resource['profile'] not in exempt
            if not applied:
                assert after == ref, (rid, key, 'exempt scenario changed')
                continue
            assert near(after['lowest_y'], max(0, ref['lowest_y'])), (rid, key, after['lowest_y'])
            assert near(after['highest_floor_y'], max(0, ref['highest_floor_y'])), (rid, key)
            assert near(after['greatest_root_lift'], max(0, -ref['lowest_y'])), (rid, key)
            for a, b in [(after, ref)] + list(zip(after['bones'], ref['bones'])):
                for limit in ('low', 'high'):
                    assert [a[limit][i] for i in (0, 2)] == [b[limit][i] for i in (0, 2)], (rid, key, 'XZ changed')
            fixes += ref['lowest_y'] < -1e-6
        rows.append({'id': rid, 'corrected_ground_scenarios': fixes,
                     'all_xz_envelopes_unchanged': True, 'exempt_scenarios_exact': True})
    report = {'schema': 'creature-native-bone-clearance-audit.v1',
              'scope': 'Dense sampled floor and geometry envelopes, not stance/stride/terrain/performance acceptance',
              'resource_count': len(rows), 'same_executable_and_inputs': True,
              'raw_control_matches_previous_kinematics': True,
              'no_negative_ground_floor_beyond_roundoff': True,
              'positive_clearance_and_exempt_scenarios_preserved': True, 'resources': rows}
    (out/'clearance_audit.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
