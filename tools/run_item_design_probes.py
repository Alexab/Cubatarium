"""Release native probes of staged designs, restoring runtime files on exit.

Does not publish assets or change gameplay definitions. Only dedicated QA worlds
are accepted. Full artistic acceptance, world integration and performance gates
remain separate from the icon and presentation probes.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import sys
from contextlib import contextmanager

from itemgen.design import build_design, ROOT
from run_items_acceptance import ITEM_TARGETS, EXTENDED_TARGETS
from qa_report_artifacts import reset_report


@contextmanager
def runtime_designs(staged,root=ROOT):
    backup={}
    try:
        for id,path in staged.items():
            if not id or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789_' for c in id):
                raise ValueError('Invalid runtime item ID')
            for name in ('model.gltf','model.bin','textures/diffuse.png'):
                runtime=root/'bin/models/items'/id/name
                backup[runtime]=runtime.read_bytes()
                runtime.write_bytes((path/name).read_bytes())
        yield len(backup)
    finally:
        for path,data in backup.items(): path.write_bytes(data)
        print(f'Restored {len(backup)} runtime files',flush=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--world',default='CarryAimQA')
    parser.add_argument('--skip-build',action='store_true')
    parser.add_argument('--fp-id',default='bucket')
    parser.add_argument('--fp-only',action='store_true',help='Presentation probe only; does not claim full native regression')
    parser.add_argument('--presentation-only',action='store_true',help='Native attachment/FP/loose inspection only; no gameplay regression claim')
    parser.add_argument('--id',action='append',dest='selected',help='Limit candidate substitutions and presentation inspection to these authored IDs')
    args=parser.parse_args()
    if not args.world.endswith('QA') or '/' in args.world or '\\' in args.world:
        parser.error('Use a dedicated world ending in QA')
    ids=sorted(p.parent.name for p in (ROOT/'tools/item_designs').glob('*/design.yaml'))
    if args.fp_id not in ids: parser.error('--fp-id must have an authored design')
    if args.presentation_only and args.fp_only: parser.error('Choose only one narrow probe scope')
    if args.selected:
        if not args.presentation_only: parser.error('--id requires --presentation-only')
        if any(id not in ids for id in args.selected): parser.error('--id must have an authored design')
        ids=sorted(set(args.selected))
    summary_path=ROOT/'bin/items_acceptance'/('presentation-probe.json' if args.presentation_only else 'design-probe.json')
    summary_path.parent.mkdir(parents=True,exist_ok=True)
    summary_path.unlink(missing_ok=True)
    logs=ROOT/'build';logs.mkdir(exist_ok=True)
    def run(command,name,env=None):
        print(name,flush=True)
        with (logs/(name+'.log')).open('w',encoding='utf-8') as log:
            subprocess.run([str(p) for p in command],cwd=ROOT,env=env,
                           stdout=log,stderr=subprocess.STDOUT,check=True)
    if not args.skip_build:
        run(['cmake','--build','build/desktop-msvc','--config','Release','--target','Cubatarium','--parallel','8'],'item-design-desktop-release')
        run(['cmake','--build','build/desktop-msvc','--config','Release','--target',*ITEM_TARGETS,*EXTENDED_TARGETS,'--parallel','8'],'item-design-tests-release')
    exe=ROOT/'bin/Cubatarium.exe'
    if not exe.is_file(): parser.error('Build the Release executable first')
    staged={id:build_design(id) for id in ids}
    env=os.environ.copy();env['PYTHONIOENCODING']='utf-8'
    # Ambient diagnostic switches from a developer shell must not alter this run.
    for key in ('CUBA_FLIGHT_QA_FP_ITEM','CUBA_ITEMS_INVENTORY_QA','CUBA_ITEMS_SURVIVAL_LOOP_QA','CUBA_ITEMS_CARRY_VISUAL_QA','CUBA_OWNERSHIP_CATALOG_VISUAL_QA','CUBA_ITEM_LAB_QA','CUBA_ITEM_PRESENTATION_QA','CUBA_ITEM_PRESENTATION_IDS'):
        env.pop(key,None)
    if args.presentation_only:
        report=summary_path.with_name('design-presentations.json')
        reset_report(report,summary_path.parent)
        env['CUBA_ITEM_PRESENTATION_QA']=str(report)
        if args.selected: env['CUBA_ITEM_PRESENTATION_IDS']=','.join(ids)
        with runtime_designs(staged) as restored_count:
            run([sys.executable,'tools/flight_sim_run.py','--world',args.world,'--require-existing-world','--seconds','3','--process-timeout',str(180+6*len(ids)),'--no-fly','--no-hold-forward','--idle-sec','0','--report',logs/'item-design-presentations-flight.json'],'item-design-presentations',env)
            coverage=json.loads(report.read_text(encoding='utf-8'))
            if not coverage.get('passed') or not set(ids).issubset(coverage.get('surface_ids',[])):
                raise RuntimeError('Presentation probe failed or substituted coverage is incomplete')
        summary=dict(passed=True,scope='presentation_only',world=args.world,staged_ids=ids,inspected_surface_ids=coverage['surface_ids'],
                     presentation_checks=len(coverage['checks']),restored_runtime_files=restored_count,
                     executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest())
        summary_path.write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
        print('Native presentation-only probe passed; gameplay/artistic acceptance is separate')
        return 0
    with runtime_designs(staged) as restored_count:
        if not args.fp_only:
            run([sys.executable,'tools/run_items_acceptance.py','--skip-build','--extended','--world',args.world],'item-design-native',env)
            result=json.loads((ROOT/'bin/items_acceptance/result.json').read_text(encoding='utf-8'))
            coverage=result['reports']['presentations']
            if sorted(coverage.get('surface_ids',[]))!=ids:
                raise RuntimeError('Native presentation coverage does not match substituted designs')
        capture=ROOT/'bin/item_surface_fp';capture.mkdir(exist_ok=True)
        for old in capture.glob('frame_*.png'): old.unlink()
        env.update(CUBA_FLIGHT_QA_FP_ITEM=args.fp_id,
                   CUBA_FLIGHT_CAPTURE_DIR=str(capture),
                   CUBA_FLIGHT_CAPTURE_INTERVAL_SEC='3')
        run([sys.executable,'tools/flight_sim_run.py','--world',args.world,'--require-existing-world','--seconds','12','--no-fly','--no-hold-forward','--idle-sec','2','--report',logs/'item-design-fp.json'],'item-design-fp',env)
        frames=sorted(capture.glob('frame_*.png'))
        if not frames: raise RuntimeError('FP probe produced no fresh frames')
        summary=dict(passed=True,scope='fp_only' if args.fp_only else 'native_regression_and_fp',world=args.world,staged_ids=ids,fp_id=args.fp_id,
                     fp_frames=[p.relative_to(ROOT).as_posix() for p in frames],
                     executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest())
    if not args.fp_only:
        summary['presentation_checks']=len(coverage['checks'])
        summary['surface_ids']=coverage['surface_ids']
    summary['restored_runtime_files']=restored_count
    summary_path.write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
    print(f"Native {summary['scope']} probes passed; artistic acceptance is separate")
    return 0


if __name__=='__main__': raise SystemExit(main())
