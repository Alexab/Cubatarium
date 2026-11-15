"""Release Item Lab diagnostics/UI on a dedicated world; no production overlay."""
from pathlib import Path
import argparse
import hashlib
import json
import os
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
    args = parser.parse_args()
    if not args.world.endswith('QA') or '/' in args.world or '\\' in args.world:
        parser.error('Use an existing dedicated QA world')
    output = ROOT / 'bin/item_lab_diagnostics'
    output.mkdir(parents=True, exist_ok=True)
    summary_path = output / 'summary.json'
    summary_path.unlink(missing_ok=True)
    report = output / 'qa.json'
    reset_report(report, output)
    before = source_digest()
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
        run([sys.executable, 'tools/flight_sim_run.py', '--world', args.world,
             '--require-existing-world', '--seconds', '12', '--process-timeout', str(240 + 6 * len(ids)),
             '--no-fly', '--no-hold-forward', '--idle-sec', '0', '--report', output / 'flight.json'], 'native-flight', env)
        native = json.loads(report.read_text(encoding='utf-8'))
        if native.get('passed') is not True or sorted(native.get('diagnostics', {})) != ids:
            raise ValueError('Native Item Lab failed or does not cover the whole catalog')
        result.update(passed=True, checks=len(native['checks']), catalog_ids=ids,
                      executable_sha256=hashlib.sha256((ROOT / 'bin/Cubatarium.exe').read_bytes()).hexdigest(),
                      report_sha256=hashlib.sha256(report.read_bytes()).hexdigest(),
                      diagnostic_frames=len(list(Path(str(report) + '.frames/diagnostics').glob('*.png'))))
    except Exception as error:
        result['error'] = str(error)
        print(error, file=sys.stderr)
    finally:
        result['sources_unchanged'] = before == source_digest()
        result['passed'] = result['passed'] and result['sources_unchanged']
        summary_path.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('passed=' + str(result['passed']) + '; evidence=' + str(output), flush=True)
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
