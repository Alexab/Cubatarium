"""Paired Release whole-frame item benchmarks on a restored dedicated QA world.

Both modes use the same executable and original world snapshot. Only runtime
model dependencies are substituted; source assets are never published.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import uuid

from itemgen.design import ROOT
from itemgen.design_publication import validate_design
from run_item_design_probes import runtime_designs


def hashes(folder):
    return {p.relative_to(folder).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(folder.rglob('*')) if p.is_file()}


def percentile(values, fraction):
    rows=sorted(values);index=(len(rows)-1)*fraction;low=int(index)
    return rows[low]+(rows[min(low+1,len(rows)-1)]-rows[low])*(index-low)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--world',default='ItemVisualPerfQA')
    parser.add_argument('--scene',action='append',choices=['fp','armor','drops','catalog'])
    parser.add_argument('--repeats',type=int,default=1)
    parser.add_argument('--skip-build',action='store_true')
    parser.add_argument('--trace-pass',action='store_true')
    args=parser.parse_args()
    if not args.world.endswith('QA') or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-' for c in args.world):
        parser.error('Use a dedicated world ending in QA')
    if not 1<=args.repeats<=5:parser.error('--repeats must be 1..5')
    output=ROOT/'bin/items_performance';output.mkdir(exist_ok=True)
    summary_path=output/'summary.json';summary_path.unlink(missing_ok=True)
    scenes=args.scene or ['fp','armor','drops','catalog']
    if not args.skip_build:
        subprocess.run(['cmake','--build','build/desktop-msvc','--config','Release','--target','Cubatarium','--parallel','8'],cwd=ROOT,check=True)
    ids=sorted(p.parent.name for p in (ROOT/'tools/item_designs').glob('*/design.yaml'))
    staged={id:validate_design(id)['folder'] for id in ids}
    references={id:(ROOT/'models/items'/id/'_itemgen_reference'
                    if (ROOT/'models/items'/id/'_itemgen_reference/model.gltf').is_file()
                    else ROOT/'models/items'/id) for id in ids}
    world_parent=(ROOT/'bin/worlds').resolve();world=(world_parent/args.world).resolve()
    if world.parent!=world_parent:parser.error('Invalid QA world path')
    if not world.exists() and args.world=='ItemVisualPerfQA':
        subprocess.run([str(ROOT/'bin/Cubatarium.exe'),'--console','--create-world','--name',args.world,
                        '--seed','15112026','--generator','flat','--radius-chunks','2'],cwd=ROOT/'bin',check=True)
    if not world.is_dir():parser.error('Existing dedicated flat QA world required')
    base=(ROOT/'build/item-performance-backups').resolve();base.mkdir(exist_ok=True)
    owned=base/uuid.uuid4().hex;owned.mkdir()
    backup=owned/'world'
    before_world=hashes(world);before_runtime=hashes(ROOT/'bin/models/items')
    config=ROOT/'bin/config.json';before_config=config.read_bytes() if config.exists() else None
    if before_config is not None:(owned/'config.json').write_bytes(before_config)
    shutil.copytree(world,backup)
    if hashes(backup)!=before_world:raise RuntimeError('QA world backup verification failed; original world retained')
    def restore_world():
        # Both recursive-delete targets are resolved and checked before use.
        target=world.resolve()
        if target.parent!=world_parent or target.name!=args.world or not target.name.endswith('QA'):
            raise ValueError('Unsafe QA world restore target')
        if target.exists():shutil.rmtree(target)
        shutil.copytree(backup,target)
    results=[];summary=dict(passed=False,runs=results,world_restored=False,runtime_restored=False)
    try:
        for scene in scenes:
            for repeat in range(args.repeats):
                modes=['baseline','candidate'] if repeat%2==0 else ['candidate','baseline']
                for mode in modes:
                    restore_world()
                    if scene=='armor':
                        # Load real human NPCs through the existing save loader;
                        # manual spawning intentionally reserves 'human' for the player.
                        # Only the restored dedicated QA copy is edited, never its backup.
                        creatures=json.loads((world/'creatures.json').read_text(encoding='utf-8'))
                        creatures['creatures']=[dict(id=500000+i,type='human',spawn_source='qa',
                            body_origin=[(i%4-1.5)*1.8,4.501,7+(i//4)*2],yaw=-90,pitch=0) for i in range(12)]
                        (world/'creatures.json').write_text(json.dumps(creatures),encoding='utf-8')
                        for snapshot in ('ownership_snapshot.json','ownership_snapshot.previous.json'):
                            (world/snapshot).unlink(missing_ok=True)
                    if before_config is not None:config.write_bytes(before_config)
                    name=f'{scene}-{repeat}-{mode}';folder=output/name;folder.mkdir(exist_ok=True)
                    capture=folder/'captures';capture.mkdir(exist_ok=True)
                    for stale in capture.glob('frame_*.png'):stale.unlink()
                    report=folder/'frame-benchmark.json';report.unlink(missing_ok=True)
                    env=os.environ.copy()
                    env.pop('CUBA_CREATURE_PASS_TIMING_PATH',None)
                    for key in list(env):
                        if key.startswith(('CUBA_ITEMS_','CUBA_ITEM_','CUBA_FLIGHT_QA_','CUBA_OWNERSHIP_')):env.pop(key)
                    env.update(PYTHONIOENCODING='utf-8',CUBA_ITEM_PERF_QA=str(report),CUBA_ITEM_PERF_SCENE=scene,
                               CUBA_FLIGHT_CAPTURE_DIR=str(capture),CUBA_FLIGHT_CAPTURE_INTERVAL_SEC='8',CUBATARIUM_PERF_LOG='0')
                    if args.trace_pass:
                        trace=folder/'creature-pass.jsonl';trace.unlink(missing_ok=True)
                        env['CUBA_CREATURE_PASS_TIMING_PATH']=str(trace)
                    print('Measuring '+name,flush=True)
                    with runtime_designs(staged if mode=='candidate' else references) as replaced:
                        with (folder/'flight.log').open('w',encoding='utf-8') as log:
                            subprocess.run([sys.executable,'tools/flight_sim_run.py','--world',args.world,'--require-existing-world',
                                '--seconds','180','--process-timeout','240','--no-fly','--no-hold-forward','--idle-sec','0',
                                '--artifact-root',str(folder/'flight-artifacts'),'--report',str(folder/'flight.json')],
                                cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
                        data=json.loads(report.read_text(encoding='utf-8'))
                        if not data.get('passed'):raise RuntimeError('Invalid measurement scene: '+name)
                        if data['runtime_v3_count']!=(len(ids) if mode=='candidate' else 0):raise RuntimeError('Wrong resource set: '+name)
                    wall=[row['wall_ms'] for row in data['frames']]
                    row=dict(scene=scene,repeat=repeat,mode=mode,frames=len(wall),p50_ms=percentile(wall,.5),
                             p95_ms=percentile(wall,.95),p99_ms=percentile(wall,.99),
                             cold_catalog_ms=data['cold_catalog_ms'],item_texture_rgba_mip_bytes=data['item_texture_rgba_mip_bytes'],
                             runtime_files_restored=replaced,report=report.relative_to(ROOT).as_posix())
                    results.append(row);print(json.dumps(row),flush=True)
        comparisons=[]
        for scene in scenes:
            old=[r['p95_ms'] for r in results if r['scene']==scene and r['mode']=='baseline']
            new=[r['p95_ms'] for r in results if r['scene']==scene and r['mode']=='candidate']
            baseline=percentile(old,.5);candidate=percentile(new,.5)
            comparisons.append(dict(scene=scene,baseline_p95_ms=baseline,candidate_p95_ms=candidate,
                                    ratio=candidate/baseline,within_ten_percent=candidate<=baseline*1.1))
        summary.update(passed=all(c['within_ten_percent'] for c in comparisons),comparisons=comparisons,
                       executable_sha256=hashlib.sha256((ROOT/'bin/Cubatarium.exe').read_bytes()).hexdigest(),
                       world=args.world,world_snapshot_hashes=before_world,
                       memory_scope='Item texture allocations only, excluding creature textures; uncompressed RGBA mip texels, not driver VRAM')
    except Exception as error:
        summary['error']=str(error);print(str(error),file=sys.stderr)
    finally:
        restore_world()
        if before_config is None:config.unlink(missing_ok=True)
        else:config.write_bytes(before_config)
        summary['world_restored']=hashes(world)==before_world
        summary['runtime_restored']=hashes(ROOT/'bin/models/items')==before_runtime
        summary['config_restored']=(config.read_bytes() if config.exists() else None)==before_config
        summary['passed']=summary['passed'] and all(summary[k] for k in ('world_restored','runtime_restored','config_restored'))
        summary_path.write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
        if summary['world_restored'] and summary['config_restored']:
            target=owned.resolve()
            if target.parent!=base or target.name!=owned.name:raise ValueError('Unsafe backup cleanup')
            shutil.rmtree(target)
        else:print('Preserved verified backup: '+str(owned),file=sys.stderr)
    print('passed='+str(summary['passed'])+'; evidence='+str(summary_path),flush=True)
    return 0 if summary['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
