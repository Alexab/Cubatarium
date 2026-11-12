"""Serial pinned-core ABBA Release sampler experiments; not world/GPU acceptance."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import statistics
import subprocess
import time
import psutil

ROOT = Path(__file__).resolve().parents[2]


def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--linear-exe', type=Path, required=True)
    parser.add_argument('--indexed-exe', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--rounds', type=int, default=4)
    args = parser.parse_args(); out = args.out.resolve()
    assert 2 <= args.rounds <= 8 and out.is_relative_to((ROOT / 'docs/research').resolve()) and not out.exists()
    out.mkdir(parents=True)
    executables = {'linear': args.linear_exe.resolve(), 'indexed': args.indexed_exe.resolve()}
    assets = {'before': ROOT / 'assetgen_output/creatures/orc_sway_pre_publish/models/creatures/orc/model.gltf',
              'current': ROOT / 'models/creatures/orc/model.gltf'}
    available = psutil.Process().cpu_affinity(); core = available[min(2, len(available) - 1)]
    context = {'scope': 'Pinned single-core serialized ABBA CPU sampler, warm loads and sampled process working set; not world frame/GPU/total application memory acceptance',
               'cpu_affinity': [core], 'logical_cpu_count': psutil.cpu_count(), 'processor': platform.processor(),
               'platform': platform.platform(), 'rounds': args.rounds,
               'executables': {k: {'path': str(p), 'sha256': sha(p)} for k, p in executables.items()},
               'assets': {k: {'path': str(p), 'gltf_sha256': sha(p), 'bin_sha256': sha(p.parent / 'model.bin')} for k, p in assets.items()},
               'runner_sha256': sha(Path(__file__))}
    (out / 'context.json').write_text(json.dumps(context, indent=2) + '\n')
    runs = []
    for round_index in range(args.rounds):
        order = [('linear', 'before'), ('indexed', 'before'), ('indexed', 'current'), ('linear', 'current')]
        if round_index % 2: order = [('indexed', 'current'), ('linear', 'current'), ('linear', 'before'), ('indexed', 'before')]
        for implementation, asset in order:
            label = f'r{round_index}_{implementation}_{asset}'
            report = out / (label + '.json'); log = out / (label + '.log')
            before_cpu = psutil.cpu_percent(interval=.1)
            with log.open('w') as stream:
                process = subprocess.Popen([str(executables[implementation]), str(report), str(assets[asset])],
                                           cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
                child = psutil.Process(process.pid); child.cpu_affinity([core])
                maximum_rss = 0; peak_wset = 0
                while process.poll() is None:
                    try:
                        memory = child.memory_info()
                        maximum_rss = max(maximum_rss, memory.rss)
                        peak_wset = max(peak_wset, getattr(memory, 'peak_wset', 0))
                    except psutil.NoSuchProcess: break
                    time.sleep(.02)
                assert process.wait() == 0, label
            data = json.loads(report.read_text())
            assert len(data['resources']) == 1
            runs.append({'round': round_index, 'implementation': implementation, 'asset': asset,
                         'report': report.name, 'sha256': sha(report), 'before_host_cpu_percent': before_cpu,
                         'sampled_max_rss_bytes': maximum_rss, 'observed_peak_wset_bytes': peak_wset,
                         'resource': data['resources'][0]})
            print(label, 'done', flush=True)
    # Same asset must have bit-identical sampled palettes/root matrices in both
    # implementations and every run. Before/current assets deliberately differ.
    for asset in assets:
        group = [r for r in runs if r['asset'] == asset]
        signatures = [[(c['clip'], c['fade'], c['palette_signature']) for c in r['resource']['cases']] for r in group]
        assert all(s == signatures[0] for s in signatures)
        assert len({r['resource']['root_sampler_signature'] for r in group}) == 1
        assert len({r['resource']['accounted_vector_string_heap_bytes'] for r in group}) == 1
    rows = []
    for implementation in executables:
        for asset in assets:
            group = [r for r in runs if r['implementation'] == implementation and r['asset'] == asset]
            for case in range(4):
                values = [r['resource']['cases'][case] for r in group]
                rows.append({'implementation': implementation, 'asset': asset, 'clip': values[0]['clip'], 'fade': values[0]['fade'],
                    'median_run_batch_p50_us_per_pose': statistics.median(v['batch_average_microseconds_per_pose']['p50'] for v in values),
                    'median_run_batch_p95_us_per_pose': statistics.median(v['batch_average_microseconds_per_pose']['p95'] for v in values),
                    'run_batch_p95_range_us_per_pose': [min(v['batch_average_microseconds_per_pose']['p95'] for v in values), max(v['batch_average_microseconds_per_pose']['p95'] for v in values)]})
    memory = []
    for asset in assets:
        group = [r for r in runs if r['asset'] == asset]
        memory.append({'asset': asset, 'accounted_vector_string_heap_bytes': group[0]['resource']['accounted_vector_string_heap_bytes'],
                       'median_process_peak_wset_bytes': statistics.median(r['observed_peak_wset_bytes'] for r in group),
                       'median_run_warm_loader_p50_ms': statistics.median(r['resource']['warm_loader_milliseconds']['p50'] for r in group)})
    summary = {'passed': True, 'scope': context['scope'], 'matrix_signatures_bit_exact': True,
               'quantile_scope': 'Each run81 batches of128 poses; table is median of run batch-average p50/p95, not individual pose/frame p95',
               'rows': rows, 'memory_load': memory, 'runs': runs, 'context_sha256': sha(out / 'context.json')}
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps({k: v for k, v in summary.items() if k != 'runs'}, indent=2))


if __name__ == '__main__': main()
