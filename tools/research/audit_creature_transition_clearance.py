"""Compare actual retained visual traces across the render-only glTF floor fix."""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    source_before = read(out / 'transition_before_sources.json')
    report = {'schema': 'creature-native-transition-clearance-audit.v1', 'passed': True,
              'scope': 'Actual cold-entry retained G visual CPU submissions,12 clip pairs/16 outgoing phases/11 fade steps where available, supported/free fixtures; not GPU crossfade raster, sliding, terrain, every clip history or performance acceptance',
              'catalogs': [], 'frame_count': 0, 'palette_bit_exact_count': 0, 'ordinary_rgba_exact_count': 0}
    expected_counts = {'badger': 196, 'treeman': 196, 'lobster': 100, 'seahorse': 4}
    for catalog, before_label, before_dir, after_label, after_dir in (
        ('published', 'transitions_published_before', 'native_transitions_published_before',
         'transitions_published_after', 'native_transitions_published_after'),
        ('proposal_v4', 'transitions_proposal_before', 'native_transitions_proposal_before',
         'envelope_current', 'native_current')):
        contexts = [read(out / f'context_{label}.json') for label in (before_label, after_label)]
        assert contexts[0]['exe_sha256'] == source_before['exe_sha256']
        assert contexts[1]['exe_sha256'] == sha(ROOT / 'bin/Cubatarium.exe')
        assert contexts[0]['exe_sha256'] != contexts[1]['exe_sha256']
        assert contexts[0]['files_sha256'] == contexts[1]['files_sha256']
        assert contexts[0]['models_root'] == contexts[1]['models_root']
        assert contexts[0]['shaders_sha256'] == contexts[1]['shaders_sha256']
        for name, digest in contexts[1]['files_sha256'].items():
            assert sha(Path(contexts[1]['models_root']) / name) == digest
        folders = [out / name for name in (before_dir, after_dir)]
        native = [read(folder / 'transition_probe.json') for folder in folders]
        for context, folder in zip(contexts, folders):
            assert context['native_transition_probe']
            assert (folder / 'transition_probe.json').stat().st_mtime >= datetime.fromisoformat(context['captured_at_utc']).timestamp()
        resources = [{r['id']: r for r in data['resources']} for data in native]
        assert set(resources[0]) == set(resources[1]) == set(expected_counts)
        catalog_report = {'id': catalog, 'before_exe': contexts[0]['exe_sha256'], 'after_exe': contexts[1]['exe_sha256'],
                          'resources': [], 'native_sha256': [sha(folder / 'transition_probe.json') for folder in folders]}
        for sid in resources[0]:
            before_cases, after_cases = [r[sid]['cases'] for r in resources]
            assert len(before_cases) == len(after_cases) == expected_counts[sid]
            min_before = min_after = float('inf')
            fixed = free = positive = 0
            for before_case, after_case in zip(before_cases, after_cases):
                for key in ('from', 'to', 'outgoing_phase', 'supported', 'in_fluid', 'on_fluid_bottom'):
                    assert before_case[key] == after_case[key]
                assert len(before_case['frames']) == len(after_case['frames']) == 11
                for a, b in zip(before_case['frames'], after_case['frames']):
                    assert a['step'] == b['step']
                    assert a['palette_hash'] == b['palette_hash'], (catalog, sid, 'authored palette changed')
                    assert a['draws'] == b['draws'] > 0
                    assert [a['low'][k] for k in (0, 2)] == [b['low'][k] for k in (0, 2)]
                    assert [a['high'][k] for k in (0, 2)] == [b['high'][k] for k in (0, 2)]
                    report['frame_count'] += 1
                    report['palette_bit_exact_count'] += 1
                    shift = b['low'][1] - a['low'][1]
                    assert abs(shift - (b['high'][1] - a['high'][1])) < 2e-6, (catalog, sid, 'not one common root lift')
                    assert shift >= -2e-6, 'render correction pulled geometry downward'
                    if before_case['supported']:
                        min_before = min(min_before, a['low'][1])
                        min_after = min(min_after, b['low'][1])
                        assert b['low'][1] > -.001, (catalog, sid, 'material ground penetration remains')
                        if a['low'][1] < -.001:
                            fixed += 1
                        if a['low'][1] >= .00001:
                            assert abs(shift) < 2e-6
                            positive += 1
                    else:
                        assert a['low'] == b['low'] and a['high'] == b['high'], (catalog, sid, 'free pose changed')
                        free += 1
            # Ordinary previews use fresh clip poses. The transition-only fix
            # must preserve their actual raster output, not only authored files.
            for frame in range(29):
                paths = [folder / f'{sid}_{frame}.png' for folder in folders]
                with Image.open(paths[0]) as a, Image.open(paths[1]) as b:
                    assert a.size == b.size and a.convert('RGBA').tobytes() == b.convert('RGBA').tobytes(), (catalog, sid, frame)
                report['ordinary_rgba_exact_count'] += 1
            catalog_report['resources'].append({'id': sid, 'cases': len(before_cases),
                'supported_min_before': min_before if min_before != float('inf') else None,
                'supported_min_after': min_after if min_after != float('inf') else None,
                'fixed_penetrating_frames': fixed, 'free_frames_exact': free, 'positive_clearance_frames_preserved': positive})
        report['catalogs'].append(catalog_report)
    assert report['frame_count'] == report['palette_bit_exact_count'] == 10912
    assert report['ordinary_rgba_exact_count'] == 232
    report['after_source_sha256'] = {name: sha(ROOT / name) for name in source_before['source_sha256']}
    (out / 'transition_clearance_audit.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(json.dumps({k: v for k, v in report.items() if k not in ('catalogs', 'after_source_sha256')}, indent=2))


if __name__ == '__main__':
    main()
