"""Serial actual-world Release experiment; execution validity is not R6 acceptance."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import sys
import time
import psutil

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def stats(values):
    values = sorted(values)
    if not values:
        return None
    return {'n': len(values), 'p50': statistics.median(values),
            'p95': values[int(.95 * (len(values) - 1))], 'min': values[0], 'max': values[-1]}


def assert_app_stopped():
    assert not any((p.info['name'] or '').lower() == 'cubatarium.exe'
                   for p in psutil.process_iter(['name'])), 'A Cubatarium process is still live'


def summarize(path, seconds, warmup=5.):
    rows = [json.loads(line) for line in path.read_text().splitlines() if line]
    assert rows[0]['schema'] == 'creature-pass-timing.v1' and rows[-1]['kind'] == 'end'
    frames = rows[1:-1]
    assert len(frames) == rows[-1]['frames'] and rows[-1]['dropped_frames'] == 0
    assert rows[0]['gpu_supported'] and rows[-1]['skipped_gpu_queries'] == 0
    elapsed = 0.; steady = []
    for frame in frames:
        assert frame['kind'] == 'frame' and frame['window_wall_ms'] >= 0
        elapsed += frame['dt_seconds']
        if warmup <= elapsed <= seconds - 1:
            steady.append(frame)
    assert len(steady) >= 100, 'Insufficient individual steady world frames'
    assert all(f['drawn'] == f['draw_calls'] == f['palette_uploads'] == 1 for f in steady), 'Scene draw workload changed'
    assert all(sum(f['pose_states'].values()) == 1 for f in steady)
    walk = [f for f in steady if f['pose_states'] == {'walk': 1}]
    assert len(walk) >= 60 and len(walk) < len(steady), 'Walk/pause coverage missing'
    result = {'observer_reserved_frame_bytes': rows[0]['observer_reserved_frame_bytes'],
              'total_frames': len(frames), 'steady_elapsed_filter_seconds': [warmup, seconds - 1],
              'frame_quantile_method': 'p50 median; p95 sorted[floor(.95*(n-1))], individual frames',
              'steady_frames': len(steady), 'walk_frames': len(walk), 'groups': {}}
    for name, group in [('all_steady', steady), ('walk_only', walk)]:
        assert sum(f['gpu_draw_interval_ns'] is not None for f in group) >= .99 * len(group)
        result['groups'][name] = {key: stats([f[key] for f in group if f[key] is not None])
                                 for key in ['cpu_pose_ms', 'cpu_pass_ms', 'window_wall_ms', 'swap_ms', 'gpu_draw_interval_ns']}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--seconds', type=float, default=20.)
    parser.add_argument('--warmup', type=float, default=5.)
    parser.add_argument('--world', default='World_254')
    parser.add_argument('--fixed-time', type=float)
    parser.add_argument('--species', choices=['orc', 'badger'], default='orc')
    parser.add_argument('--before-species-dir', type=Path)
    parser.add_argument('--candidate-species-dir', type=Path)
    parser.add_argument('--baseline-exe', type=Path)
    parser.add_argument('--candidate-exe', type=Path)
    args = parser.parse_args()
    out = args.out.resolve()
    assert out.is_relative_to((ROOT / 'docs/research').resolve()) and not out.exists()
    assert 16 <= args.seconds <= 40
    assert 5 <= args.warmup <= args.seconds - 5
    assert args.fixed_time is None or 0 <= args.fixed_time <= 1
    assert Path(args.world).name == args.world
    out.mkdir(parents=True)
    scratch = ROOT / 'assetgen_output/creatures/world_pass_perf_v1'
    executables = {'baseline': (args.baseline_exe or scratch / 'baseline/Cubatarium.exe').resolve(),
                   'indexed': (args.candidate_exe or scratch / 'indexed/Cubatarium.exe').resolve()}
    assert all(p != (ROOT / 'bin/Cubatarium.exe').resolve() for p in executables.values()), 'Use archived immutable binaries'
    assert all(p.is_file() for p in executables.values())
    assert_app_stopped()
    assert sha(ROOT / 'bin/Cubatarium.exe') == sha(executables['indexed']), 'Live executable is not the final indexed Release'
    before = args.before_species_dir or (ROOT / 'assetgen_output/creatures/orc_sway_pre_publish/models/creatures/orc'
                                         if args.species == 'orc' else ROOT / 'models/creatures/badger')
    candidate = args.candidate_species_dir or ROOT / 'models/creatures' / args.species
    species_sources = {'before': before.resolve(), 'current': candidate.resolve()}
    # Same complete catalog override in both assets; only the chosen species differs.
    catalogs = {}
    for asset in ['before', 'current']:
        qa_name = 'world_pass_perf_' + out.name + '_' + asset
        models = ROOT / 'assetgen_output/creatures' / qa_name / 'models'
        assert not models.exists(), 'Use a fresh experiment/catalog pair'
        shutil.copytree(ROOT / 'bin/models', models)
        source = species_sources[asset]
        for path in source.rglob('*'):
            if path.is_file():
                target = models / 'creatures' / args.species / path.relative_to(source)
                assert target.is_file(), 'Unexpected snapshot addition'
                shutil.copy2(path, target)
        # A fixed-population render experiment: ordinary natural spawns would
        # introduce unrelated species after a random interval. This fixture
        # changes catalog flags equally in both isolated copies, never bin/models.
        for definition in (models / 'creatures').glob('*/creature.json'):
            data = json.loads(definition.read_text(encoding='utf-8-sig'))
            data.setdefault('catalog', {})['natural_spawnable'] = False
            definition.write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')
        catalogs[asset] = {'qa_name': qa_name, 'models': models,
                          'hashes': {p.relative_to(models).as_posix(): sha(p) for p in models.rglob('*') if p.is_file()}}
    changed = [p for p in catalogs['current']['hashes'] if catalogs['before']['hashes'].get(p) != catalogs['current']['hashes'][p]]
    assert changed and all(p.startswith('creatures/' + args.species + '/') for p in changed)
    source_files = ['src/Creatures/Diagnostics/CreaturePassTimingProbe.cpp', 'src/Creatures/Diagnostics/CreaturePassTimingProbe.h',
                    'src/Creatures/Diagnostics/CreatureQaWorldMotion.h', 'src/Render/Engine/CreatureDrawPass.cpp',
                    'src/Render/Engine/CreatureDrawPass.h', 'src/App/Platform/WindowManager.cpp',
                    'src/Creatures/Visual/Gltf/GltfKeyframeInterval.h', 'tools/research/run_creature_pilot_qa.py',
                    'tools/research/run_creature_world_pass_bench.py', 'CMakeLists.txt']
    context = {'scope': f'One actual {args.species} in {args.world} with ordinary motor Walk/pause; 2 repeated balanced asset/lookup experiments, not all-backend/world/R6 performance acceptance',
               'species': args.species, 'seconds': args.seconds, 'warm_filter_seconds': args.warmup, 'world': args.world,
               'fixed_time_env': args.fixed_time, 'species_source_dirs': {k: str(v) for k,v in species_sources.items()},
               'fixture': 'Natural spawning disabled equally by catalog.natural_spawnable=false in isolated QA catalogs only; ordinary QA actor motor/collision/state/render remain active',
               'executables': {k: {'path': str(p), 'sha256': sha(p)} for k, p in executables.items()},
               'catalogs': {k: {**v, 'models': str(v['models'])} for k, v in catalogs.items()},
               'changed_asset_files': changed, 'source_hashes': {p: sha(ROOT / p) for p in source_files}}
    (out / 'context.json').write_text(json.dumps(context, indent=2) + '\n')
    runs = []
    env = os.environ.copy(); env.pop('CUBA_FIXED_TIME', None)
    if args.fixed_time is not None: env['CUBA_FIXED_TIME'] = str(args.fixed_time)
    try:
        order = [('baseline', 'before'), ('indexed', 'before'), ('indexed', 'current'), ('baseline', 'current')]
        for repeat in range(2):
            for implementation, asset in (order if repeat == 0 else list(reversed(order))):
                label = f'r{repeat}_{implementation}_{asset}'
                folder = out / label
                assert_app_stopped()
                shutil.copy2(executables[implementation], ROOT / 'bin/Cubatarium.exe')
                command = [sys.executable, 'tools/research/run_creature_pilot_qa.py',
                           '--qa-name', catalogs[asset]['qa_name'], '--species', args.species, '--world', args.world, '--out', str(folder),
                           '--label', label, '--native-dir', 'native', '--world-motion', 'land_walk_pause',
                           '--pass-timing', '--seconds', str(args.seconds), '--capture-interval', '60', '--close']
                samples = []; seen = set(); started = time.monotonic()
                psutil.cpu_percent(interval=None)
                with (out / (label + '.log')).open('w') as log:
                    process = subprocess.Popen(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
                    parent = psutil.Process(process.pid)
                    while process.poll() is None:
                        try:
                            children = parent.children(recursive=True)
                        except psutil.NoSuchProcess:
                            children = []
                        for child in children:
                            try:
                                if child.name().lower() != 'cubatarium.exe':
                                    continue
                                if child.pid not in seen:
                                    seen.add(child.pid); child.cpu_percent(interval=None)
                                memory = child.memory_info()
                                samples.append({'elapsed_process_observation_seconds': time.monotonic() - started,
                                                'pid': child.pid, 'rss_bytes': memory.rss,
                                                'private_bytes': getattr(memory, 'private', None),
                                                'app_cpu_percent_one_core_units': child.cpu_percent(interval=None),
                                                'host_cpu_percent': psutil.cpu_percent(interval=None)})
                            except (psutil.NoSuchProcess, psutil.AccessDenied):
                                pass
                        time.sleep(.2)
                    assert process.wait() == 0, 'Flight failed: ' + label
                assert len(seen) == 1 and samples, 'Actual app process sampling failed'
                (out / (label + '_host.json')).write_text(json.dumps(samples, indent=2) + '\n')
                row = {'label': label, 'repeat': repeat, 'implementation': implementation, 'asset': asset,
                       'timing': summarize(folder / f'pass_timing_{label}.jsonl', args.seconds, args.warmup),
                       'whole_process_observed_rss_bytes': stats([s['rss_bytes'] for s in samples]),
                       'whole_process_observed_host_cpu_percent': stats([s['host_cpu_percent'] for s in samples])}
                runs.append(row)
                (out / 'runs_partial.json').write_text(json.dumps(runs, indent=2) + '\n')
                print(label, 'validated', flush=True)
    finally:
        assert_app_stopped()
        shutil.copy2(executables['indexed'], ROOT / 'bin/Cubatarium.exe')
        assert sha(ROOT / 'bin/Cubatarium.exe') == sha(executables['indexed'])
    (out / 'summary.json').write_text(json.dumps({'execution_and_workload_validated': True,
        'performance_gate': 'not_granted; inspect repeat noise, scope and individual groups',
        'context_sha256': sha(out / 'context.json'), 'runs': runs,
        'final_indexed_bin_restored': True}, indent=2) + '\n')


if __name__ == '__main__':
    main()
