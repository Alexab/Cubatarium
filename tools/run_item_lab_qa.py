"""Release Item Lab diagnostics/UI on a dedicated world; no production overlay."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import tempfile
import subprocess
import sys

from itemgen.design import ROOT, build_design
from qa_report_artifacts import reset_report


def source_digest():
    digest = hashlib.sha256()
    for folder in ('content/items', 'tools/item_recipes', 'tools/item_designs', 'models/items'):
        for path in sorted((ROOT / folder).rglob('*')):
            if path.is_file():
                digest.update(path.relative_to(ROOT).as_posix().encode('utf-8'))
                digest.update(b'\0')
                digest.update(hashlib.sha256(path.read_bytes()).digest())
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--world', default='CarryAimQA')
    parser.add_argument('--skip-build', action='store_true', help='Reuse existing Release binaries; still build authored candidates and run checks')
    parser.add_argument('--width', type=int, choices=[1280, 1920])
    parser.add_argument('--scale', type=float, choices=[1., 1.5, 2.], default=1.)
    parser.add_argument('--art-gallery', action='store_true', help='Also capture all-item native reference/candidate artistic comparison views')
    args = parser.parse_args()
    if not args.world.endswith('QA') or '/' in args.world or '\\' in args.world:
        parser.error('Use an existing dedicated QA world')
    output = ROOT / ('bin/item_lab_art' if args.art_gallery else 'bin/item_lab_diagnostics')
    if args.width:
        output = output / f'{args.width}-{int(args.scale*100)}'
    output.mkdir(parents=True, exist_ok=True)
    summary_path = output / 'summary.json'
    summary_path.unlink(missing_ok=True)
    report = output / 'qa.json'
    reset_report(report, output)
    before = source_digest()
    def hashes(folder):
        return {p.relative_to(folder).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in sorted(folder.rglob('*')) if p.is_file()}
    world_parent = (ROOT/'bin/worlds').resolve()
    world = (world_parent/args.world).resolve()
    if world.parent != world_parent or not world.is_dir():
        parser.error('Existing dedicated QA world required')
    before_world = hashes(world)
    config = ROOT/'bin/config.json'
    before_config = config.read_bytes() if config.exists() else None
    backup_root = (ROOT/'build/item-lab-backups').resolve()
    backup_root.mkdir(exist_ok=True)
    backup = Path(tempfile.mkdtemp(prefix='snapshot-', dir=backup_root)).resolve()
    shutil.copytree(world, backup/'world')
    if hashes(backup/'world') != before_world:
        raise RuntimeError('QA world backup verification failed')
    result = dict(passed=False, world=args.world, scope='Item Lab native diagnostics/UI; no production asset substitutions')
    try:
        def run(command, name, env=None):
            print(name, flush=True)
            with (output / (name + '.log')).open('w', encoding='utf-8') as log:
                subprocess.run([str(part) for part in command], cwd=ROOT, env=env,
                               stdout=log, stderr=subprocess.STDOUT, check=True)
        if not args.skip_build:
            run(['cmake', '--build', 'build/desktop-msvc', '--config', 'Release', '--target', 'Cubatarium', '--parallel', '8'], 'desktop-release')
            run(['cmake', '--build', 'build/desktop-msvc', '--config', 'Release', '--target', 'item_render_asset_test', '--parallel', '8'], 'unit-release')
        run(['ctest', '--test-dir', 'build/desktop-msvc', '-C', 'Release', '-R', '^item_render_asset_test$', '--no-tests=error', '--output-on-failure'], 'ctest')
        ids = sorted(path.stem for path in (ROOT / 'content/items').glob('*.json'))
        for id in ids:
            build_design(id)
        env = os.environ.copy()
        env['PYTHONIOENCODING'] = 'utf-8'
        for key in list(env):
            if key.startswith('CUBA_') and ('QA' in key or key.startswith('CUBA_FLIGHT')):
                env.pop(key)
        env['CUBA_ITEM_LAB_QA'] = str(report)
        if args.width:
            request = dict(width=args.width, height=720 if args.width == 1280 else 1080, scale=args.scale)
            display_path = output/'display-request.json'
            display_path.write_text(json.dumps(request), encoding='utf-8')
            env['CUBA_ITEM_LAB_DISPLAY_QA'] = str(display_path)
        if args.art_gallery:
            env['CUBA_ITEM_LAB_ART_QA'] = '1'
        run([sys.executable, 'tools/flight_sim_run.py', '--world', args.world,
             '--require-existing-world', '--seconds', '12', '--process-timeout', str(240 + (12 if args.art_gallery else 6) * len(ids)),
             '--no-fly', '--no-hold-forward', '--idle-sec', '0', '--report', output / 'flight.json'], 'native-flight', env)
        native = json.loads(report.read_text(encoding='utf-8'))
        if native.get('passed') is not True or sorted(native.get('diagnostics', {})) != ids:
            raise ValueError('Native Item Lab failed or does not cover the whole catalog')
        if args.width and any(native.get('display', {}).get(k) != v for k, v in request.items()):
            raise ValueError('Native Item Lab display metrics differ from the request')
        if args.art_gallery:
            gallery = native.get('art_gallery', {})
            if sorted(gallery) != ids or any(len(row.get('frames', [])) != 18 or not row.get('staged_candidate') for row in gallery.values()):
                raise ValueError('Art gallery must cover every staged item and both sources in nine views')
            frame_root = Path(str(report) + '.frames/art')
            for row in gallery.values():
                for name in row['frames']:
                    if Path(name).name != name or not (frame_root / name).is_file():
                        raise ValueError('Missing or unsafe art-gallery frame: ' + name)
            result.update(art_gallery_frames=sum(len(row['frames']) for row in gallery.values()), artistic_acceptance='not evaluated by capture checks')
        result.update(display=native.get('display'), passed=True, checks=len(native['checks']), catalog_ids=ids,
                      executable_sha256=hashlib.sha256((ROOT / 'bin/Cubatarium.exe').read_bytes()).hexdigest(),
                      report_sha256=hashlib.sha256(report.read_bytes()).hexdigest(),
                      diagnostic_frames=len(list(Path(str(report) + '.frames/diagnostics').glob('*.png'))))
    except Exception as error:
        result['error'] = str(error)
        print(error, file=sys.stderr)
    finally:
        if world.parent != world_parent or not world.name.endswith('QA'):
            raise ValueError('Unsafe QA world restoration')
        shutil.rmtree(world)
        shutil.copytree(backup/'world', world)
        if before_config is None:
            config.unlink(missing_ok=True)
        else:
            config.write_bytes(before_config)
        result['world_restored'] = hashes(world) == before_world
        result['config_restored'] = (config.read_bytes() if config.exists() else None) == before_config
        if result['world_restored'] and result['config_restored']:
            if backup.parent != backup_root or not backup.name.startswith('snapshot-'):
                raise ValueError('Unsafe QA snapshot cleanup')
            shutil.rmtree(backup)
        result['sources_unchanged'] = before == source_digest()
        result['passed'] = result['passed'] and all(result[k] for k in ('sources_unchanged', 'world_restored', 'config_restored'))
        summary_path.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('passed=' + str(result['passed']) + '; evidence=' + str(output), flush=True)
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
