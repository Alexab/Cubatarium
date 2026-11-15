"""Capture staged item designs in actual Release world frames on a restored QA world."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from itemgen.design import ROOT
from itemgen.design_publication import validate_design
from run_item_design_probes import runtime_designs


def hashes(folder):
    return {p.relative_to(folder).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(folder.rglob('*')) if p.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--world', default='ItemVisualPerfQA')
    parser.add_argument('--id', action='append', dest='ids')
    parser.add_argument('--light', choices=['day', 'night', 'interior', 'interior-lit'], default='day')
    parser.add_argument('--width', type=int, choices=[1280, 1920], default=1280)
    parser.add_argument('--scale', type=float, choices=[1., 1.5, 2.], default=1.)
    parser.add_argument('--mode', choices=['creative', 'survival'], default='creative')
    parser.add_argument('--body', choices=['male', 'female'], default='male')
    parser.add_argument('--perspective', choices=['first', 'back', 'front'], default='first')
    parser.add_argument('--interaction', choices=['throw', 'use'], help='Route real throw or use/slot-cancel/complete input')
    parser.add_argument('--skip-build', action='store_true')
    args = parser.parse_args()
    if not args.world.endswith('QA') or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-' for c in args.world):
        parser.error('Use a dedicated existing QA world')
    defaults = ['apple', 'bread', 'raw_meat', 'cooked_meat', 'waterskin', 'wood_bow'] if args.interaction == 'use' else [p.stem for p in (ROOT/'content/items').glob('*.json')]
    ids = sorted(args.ids or defaults)
    staged = {id: validate_design(id)['folder'] for id in ids}
    if not args.skip_build:
        subprocess.run(['cmake', '--build', 'build/desktop-msvc', '--config', 'Release',
                        '--target', 'Cubatarium', '--parallel', '8'], cwd=ROOT, check=True)
    world_parent = (ROOT/'bin/worlds').resolve()
    world = (world_parent/args.world).resolve()
    if world.parent != world_parent or not world.is_dir():
        parser.error('Existing QA world required')
    name = f'{args.light}-{args.width}-{int(args.scale*100)}'
    if args.ids:
        name += '-selected-'+hashlib.sha256(','.join(ids).encode()).hexdigest()[:8]
    if args.interaction:
        name += '-'+args.interaction
    if args.perspective != 'first':
        name += '-'+args.perspective
    if args.body != 'male':
        name += '-'+args.body
    if args.mode != 'creative':
        name += '-'+args.mode
    output = ROOT/'bin/item_world_visual'/name
    output.mkdir(parents=True, exist_ok=True)
    report = output/'native.json'
    report.unlink(missing_ok=True)
    summary_path = output/'summary.json'
    summary_path.unlink(missing_ok=True)
    request = dict(ids=ids, light=args.light, width=args.width,
                   height=720 if args.width == 1280 else 1080, scale=args.scale, interaction=args.interaction or "", perspective=args.perspective, mode=args.mode, body=args.body)
    (output/'request.json').write_text(json.dumps(request, indent=2), encoding='utf-8')
    config = ROOT/'bin/config.json'
    config_before = config.read_bytes() if config.exists() else None
    before_world = hashes(world)
    before_runtime = hashes(ROOT/'bin/models/items')
    before_authors = hashes(ROOT/'tools/item_designs')
    summary = dict(passed=False, request=request, implementation_revision=subprocess.check_output(
        ['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        executable_sha256=hashlib.sha256((ROOT/'bin/Cubatarium.exe').read_bytes()).hexdigest(),
        sources={name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in
                 ['src/App/Application.cpp', 'src/Test/Native/ItemWorldVisualQa.h',
                  'tools/run_item_world_visual_qa.py',
                  'src/Creatures/Visual/WornEquipmentDrawer.cpp',
                  'src/Creatures/Visual/WornEquipmentDrawer.h',
                  'src/Test/Native/ItemPresentationQa.h',
                  'src/Creatures/Visual/CarryPresentation.h',
                  'src/Creatures/Visual/CreatureVisualBoneSkeleton.cpp',
                  'src/Creatures/Visual/CreatureVisualBoneSkeleton.h',
                  'src/Items/ItemRenderAsset.cpp', 'src/Items/ItemRenderAsset.h',
                  'src/Items/ItemPresentationMath.h', 'src/Pose/HumanArmPose.h',
                  'src/Pose/HumanBowPose.h', 'src/Pose/HumanCarryPose.h',
                  'src/Pose/CreaturePoseParams.h', 'src/Render/Engine/CreatureDrawPass.cpp',
                  'src/World/Items/WorldItemSystem.cpp',
                  'src/Creatures/Visual/CreatureDrawContext.h', 'src/Creatures/Visual/CreatureVisual.h',
                  'src/Creatures/Visual/CreatureVisualBase.cpp', 'src/Creatures/Core/Creature.h',
                  'src/App/Platform/InputManager.cpp', 'src/App/Platform/WindowManager.cpp',
                  'src/App/Input/InputRouter.cpp', 'src/Blocks/Input/BlockInputController.cpp']})
    backups = ROOT/'build/item-world-visual-backups'
    backups.mkdir(exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix='snapshot-', dir=backups))
    backup = Path(temporary)/'world'
    shutil.copytree(world, backup)
    if hashes(backup) != before_world:
        raise RuntimeError('World backup verification failed')
    try:
        env = os.environ.copy()
        for key in list(env):
            if key.startswith(('CUBA_ITEMS_', 'CUBA_ITEM_', 'CUBA_FLIGHT_QA_', 'CUBA_OWNERSHIP_')):
                env.pop(key)
        if args.interaction:
            configured = json.loads(config_before.decode('utf-8-sig')) if config_before else {}
            configured.setdefault('ui', {}).update(grab_key='v', throw_key='r', control_scheme='classic')
            config.write_text(json.dumps(configured), encoding='utf-8')
        env.update(PYTHONIOENCODING='utf-8', CUBA_ITEM_WORLD_VISUAL_QA=str(report))
        with runtime_designs(staged) as restored:
            with (output/'flight.log').open('w', encoding='utf-8') as log:
                subprocess.run([sys.executable, 'tools/flight_sim_run.py', '--world', args.world,
                                '--require-existing-world', '--seconds', str(30+len(ids)*(8 if args.interaction else 2)),
                                '--process-timeout', str(180+len(ids)*(12 if args.interaction else 3)), '--no-fly',
                                '--no-hold-forward', '--idle-sec', '0', '--artifact-root',
                                str(output/'flight-artifacts'), '--report', str(output/'flight.json')],
                               cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        data = json.loads(report.read_text(encoding='utf-8'))
        expected = [id for id in ids for _ in range(8 if args.interaction else 1)]
        if not data.get('passed') or [r['id'] for r in data['frames']] != expected:
            raise RuntimeError('Incomplete world capture coverage')
        for row in data['frames']:
            if not args.interaction and (row['active_item'] != row['id'] or not row['matching_body']):
                raise RuntimeError('World frame lost its actual inventory or physical item')
        summary.update(passed=True, frames=hashes(output), runtime_files_restored=restored)
    except Exception as error:
        summary['error'] = str(error)
        print(error, file=sys.stderr)
    finally:
        # Both targets are verified absolute children of the QA world root.
        if world.parent != world_parent or not world.name.endswith('QA'):
            raise ValueError('Unsafe QA restore target')
        shutil.rmtree(world)
        shutil.copytree(backup, world)
        if config_before is None:
            config.unlink(missing_ok=True)
        else:
            config.write_bytes(config_before)
        summary.update(sources_unchanged=all(hashlib.sha256((ROOT/name).read_bytes()).hexdigest() == value
                                           for name, value in summary['sources'].items()),
                       world_restored=hashes(world) == before_world,
                       runtime_restored=hashes(ROOT/'bin/models/items') == before_runtime,
                       authors_unchanged=hashes(ROOT/'tools/item_designs') == before_authors,
                       config_restored=(config.read_bytes() if config.exists() else None) == config_before)
        summary['passed'] = summary['passed'] and all(summary[k] for k in
            ('world_restored', 'runtime_restored', 'authors_unchanged', 'config_restored', 'sources_unchanged'))
        summary_path.write_text(json.dumps(summary, indent=2)+'\n', encoding='utf-8')
    if summary['world_restored'] and summary['config_restored']:
        target = temporary.resolve()
        if target.parent != backups.resolve() or not target.name.startswith('snapshot-'):
            raise ValueError('Unsafe QA backup cleanup path')
        shutil.rmtree(target)
    else:
        print('Preserved QA backup: '+str(temporary), file=sys.stderr)
    print('passed='+str(summary['passed'])+'; evidence='+str(summary_path))
    return 0 if summary['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
