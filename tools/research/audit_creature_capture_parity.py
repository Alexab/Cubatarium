"""Compare complete native preview captures across evidence directories.

Read-only with respect to models. Exact image equivalence preserves prior art
observations; it does not grant new artistic or full runtime acceptance.
"""
import argparse
from datetime import datetime
import hashlib
import json
import math
from pathlib import Path

from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for prefix in ('reference', 'candidate'):
        parser.add_argument('--' + prefix + '-out', type=Path, required=True)
        parser.add_argument('--' + prefix + '-label', required=True)
        parser.add_argument('--' + prefix + '-dir', required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--allow-executable-change', action='store_true',
                        help='Explicit comparison across a renderer change; report the differing executables')
    parser.add_argument('--expected-changed-frame', action='append', default=[],
                        help='Exact frame path deliberately changed; must actually differ. All other RGBA must match.')
    parser.add_argument('--expected-changed-resource', action='append', default=[],
                        help='Explicit reviewed resource revision: only these resource frames may differ; each must actually change.')
    parser.add_argument('--expected-state-time-factor', action='append', default=[],
                        help='Explicit ID:STATE:FACTOR macroloop duration change. Only time_seconds and primary_period_seconds may differ; phase/owner/backend/view metadata remain exact.')
    args = parser.parse_args()
    contexts = []
    folders = []
    for prefix in ('reference', 'candidate'):
        out = getattr(args, prefix + '_out').resolve()
        label = getattr(args, prefix + '_label')
        contexts.append(json.loads((out / f'context_{label}.json').read_text(encoding='utf8')))
        folders.append(out / getattr(args, prefix + '_dir'))
    same_executable = contexts[0]['exe_sha256'] == contexts[1]['exe_sha256']
    assert same_executable or args.allow_executable_change
    assert contexts[0]['shaders_sha256'] == contexts[1]['shaders_sha256']
    assert contexts[0]['preview_species'] == contexts[1]['preview_species']
    candidate = contexts[1]
    for name, digest in candidate['files_sha256'].items():
        assert hashlib.sha256((Path(candidate['models_root']) / name).read_bytes()).hexdigest() == digest, name
    names = [f'{sid}_{frame}.png' for sid in candidate['preview_species']
             for frame in range(35 if sid == 'human' else 29)]
    owners = {f'{sid}_{frame}.png': sid for sid in candidate['preview_species']
              for frame in range(35 if sid == 'human' else 29)}
    manifests = [json.loads((folder / 'matrix/manifest.json').read_text(encoding='utf8')) for folder in folders]
    expected_manifest = json.loads(json.dumps(manifests[0]))
    timing_changes = []
    period_changes = []
    for declaration in args.expected_state_time_factor:
        sid, state, value = declaration.split(':'); factor = float(value)
        assert sid in args.expected_changed_resource and math.isfinite(factor) and factor > 0 and factor != 1
        before = next(r for r in expected_manifest['resources'] if r['id'] == sid)
        after = next(r for r in manifests[1]['resources'] if r['id'] == sid)
        assert len(before['frames']) == len(after['frames'])
        matching = 0
        for a, b in zip(before['frames'], after['frames']):
            if a['state'] != state: continue
            assert abs(b['time_seconds'] - a['time_seconds'] * factor) < 1e-6, (sid, state, 'unexpected macroloop timing')
            timing_changes.append({'id': sid, 'state': state, 'pose': a['pose'], 'view': a['view'],
                                   'before_seconds': a['time_seconds'], 'candidate_seconds': b['time_seconds'], 'factor': factor})
            a['time_seconds'] = b['time_seconds']; matching += 1
        assert matching, (sid, state, 'declared timing change has no frames')
        if before.get('primary_state') == state:
            assert abs(after['primary_period_seconds'] - before['primary_period_seconds'] * factor) < 1e-6
            period_changes.append({'id': sid, 'state': state, 'factor': factor,
                                   'before_seconds': before['primary_period_seconds'], 'candidate_seconds': after['primary_period_seconds']})
            before['primary_period_seconds'] = after['primary_period_seconds']
    assert expected_manifest == manifests[1], 'Pose/owner/backend metadata changed'
    for resource in manifests[1]['resources']:
        for frame in resource['frames']:
            names.extend('matrix/' + frame[key] for key in ('color', 'silhouette'))
            owners.update({'matrix/' + frame[key]: resource['id'] for key in ('color', 'silhouette')})
    rows = []
    changed = []
    expected_changes = set(args.expected_changed_frame)
    expected_resources = set(args.expected_changed_resource)
    assert expected_resources <= {r['id'] for r in manifests[1]['resources']}
    assert expected_changes <= set(names), 'Expected change outside captured catalog'
    for name in names:
        paths = [folder / name for folder in folders]
        for context, path in zip(contexts, paths):
            assert path.stat().st_mtime >= datetime.fromisoformat(context['captured_at_utc']).timestamp(), path
        with Image.open(paths[0]) as before, Image.open(paths[1]) as after:
            same = before.size == after.size and before.convert('RGBA').tobytes() == after.convert('RGBA').tobytes()
            if name in expected_changes:
                assert not same, name
            elif owners[name] not in expected_resources:
                assert same, name
        row = {'path': name, 'candidate_sha256': hashlib.sha256(paths[1].read_bytes()).hexdigest()}
        if same:
            rows.append(row)
        else:
            row['reference_sha256'] = hashlib.sha256(paths[0].read_bytes()).hexdigest()
            changed.append(row)
    assert expected_resources <= {owners[row['path']] for row in changed}, 'A declared resource revision did not change any captured pixels'
    report = {'schema': 'creature-complete-capture-parity.v1',
              'scope': 'Exact sampled preview RGBA only; no world/state/performance acceptance',
              'reference_context': str(args.reference_out / f'context_{args.reference_label}.json'),
              'candidate_context': str(args.candidate_out / f'context_{args.candidate_label}.json'),
              'same_executable': same_executable, 'same_shaders': True,
              'exe_sha256': [context['exe_sha256'] for context in contexts], 'fresh_images': True,
              'current_candidate_inputs_verified': True, 'resources': len(manifests[1]['resources']),
              'legacy_png_count': sum(not name.startswith('matrix/') for name in names),
              'matrix_png_count': sum(name.startswith('matrix/') for name in names),
              'exact_rgba_png_count': len(rows), 'expected_changed_resources': sorted(expected_resources),
              'expected_state_timing_changes': timing_changes,
              'expected_primary_period_changes': period_changes,
              'expected_changed_frames': changed, 'frames': rows}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(json.dumps({k: v for k, v in report.items() if k != 'frames'}, indent=2))


if __name__ == '__main__':
    main()
