"""Recheck raw per-frame world benchmark evidence; never grant R6/performance acceptance."""
import argparse
from collections import Counter
import json
from pathlib import Path
import platform
import statistics
import psutil
from run_creature_world_pass_bench import ROOT, sha, summarize


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args(); out = args.out.resolve()
    context = json.loads((out / 'context.json').read_text())
    report = json.loads((out / 'summary.json').read_text())
    assert report['context_sha256'] == sha(out / 'context.json')
    assert report['final_indexed_bin_restored'] and len(report['runs']) == 8
    for relative, digest in context['source_hashes'].items():
        assert sha(ROOT / relative) == digest, 'Source changed: ' + relative
    for record in context['executables'].values():
        assert sha(Path(record['path'])) == record['sha256']
    assert sha(ROOT / 'bin/Cubatarium.exe') == context['executables']['indexed']['sha256']
    for catalog in context['catalogs'].values():
        for relative, digest in catalog['hashes'].items():
            assert sha(Path(catalog['models']) / relative) == digest
    rows = []
    for run in report['runs']:
        label = run['label']; folder = out / label
        capture = folder / f'pass_timing_{label}.jsonl'
        assert summarize(capture, context['seconds'], context.get('warm_filter_seconds',5)) == run['timing']
        pilot = json.loads((folder / f'context_{label}.json').read_text())
        expected = context['executables'][run['implementation']]['sha256']
        assert pilot['exe_sha256'] == expected and pilot['pass_timing'] and pilot['qa_models_override']
        assert Path(pilot['models_root']) == Path(context['catalogs'][run['asset']]['models'])
        assert pilot['world_species'] == [context.get('species','orc')] and pilot['qa_camera'] == 'close'
        assert pilot['world'] == context['world']
        process = json.loads((folder / f'report_{label}.json').read_text())
        assert process['process_success'] and not process['hang_killed']
        assert process['world_startup']['runtime_world'] == context['world']
        assert process['world_startup']['runtime_world_matches_requested']
        assert pilot['world_motion'] == 'land_walk_pause'
        motion_file = folder / f'world_motion_{label}.jsonl'
        motion = [json.loads(line) for line in motion_file.read_text().splitlines() if line]
        assert {m['species'] for m in motion} == {context.get('species','orc')} and not any(m['frozen'] for m in motion)
        assert any(m['on_ground'] and m['state'].lower() == 'walk' and m['requested_move'] for m in motion)
        assert any(not m['requested_move'] and not m['in_fluid'] for m in motion)
        frames = [json.loads(line) for line in capture.read_text().splitlines()][1:-1]
        elapsed = 0.; steady = []
        for frame in frames:
            elapsed += frame['dt_seconds']
            if context.get('warm_filter_seconds',5) <= elapsed <= context['seconds'] - 1:
                steady.append(frame)
        # Controlled user + exactly one QA Orc; no hidden population drift.
        assert {f['considered'] for f in steady} == {2}
        assert {f['culled'] for f in steady} == {1}
        host_file = out / (label + '_host.json')
        samples = json.loads(host_file.read_text())
        pids = {s['pid'] for s in samples}; assert len(pids) == 1
        logs = list((ROOT / 'bin/logs').glob('*INFO*.' + str(next(iter(pids)))))
        assert len(logs) == 1
        probe = [line for line in logs[0].read_text(encoding='utf8', errors='replace').splitlines()
                 if '[RenderBackendCaps] probe' in line]
        assert len(probe) == 1
        cameras = [[min(f['camera'][axis] for f in steady), max(f['camera'][axis] for f in steady)] for axis in range(3)]
        rows.append({'label': label, 'timing_sha256': sha(capture), 'motion_sha256': sha(motion_file),
                     'host_sha256': sha(host_file), 'considered_culled': [2, 1], 'camera_ranges': cameras,
                     'gl_probe': probe[0], 'actual_motion_states': dict(Counter(m['state'] for m in motion))})
    table = []
    for implementation in ['baseline', 'indexed']:
        for asset in ['before', 'current']:
            group = [r for r in report['runs'] if r['implementation'] == implementation and r['asset'] == asset]
            assert len(group) == 2 and {r['repeat'] for r in group} == {0, 1}
            entry = {'implementation': implementation, 'asset': asset, 'walk_only': {}}
            for metric in ['cpu_pose_ms', 'cpu_pass_ms', 'window_wall_ms', 'gpu_draw_interval_ns']:
                values = [r['timing']['groups']['walk_only'][metric] for r in group]
                p95 = [v['p95'] for v in values]
                entry['walk_only'][metric] = {'median_run_p50': statistics.median(v['p50'] for v in values),
                    'median_run_p95': statistics.median(p95), 'repeat_p95_range': [min(p95), max(p95)],
                    'repeat_p95_max_over_min': max(p95) / min(p95)}
            table.append(entry)
    result = {'evidence_integrity_passed': True, 'performance_gate': 'unproven',
              'scope': f'One fixed-population QA {context.get("species","orc")} actual-world Walk/pause; do not generalize to all species/backends/scenes',
              'processor': platform.processor(), 'logical_cpu_count': psutil.cpu_count(),
              'quantile_scope': 'Individual-frame p95 in each run; table reports median of 2 run p95 values, not pooled-frame p95',
              'summary_sha256': sha(out / 'summary.json'), 'runs': rows, 'table': table}
    (out / 'independent_audit.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'evidence_integrity_passed': True, 'runs': len(rows), 'performance_gate': 'unproven'}))


if __name__ == '__main__':
    main()
