"""Release-only item/inventory/throw regression runner with native QA fixtures."""
from pathlib import Path
import argparse
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
ITEM_TARGETS=['item_render_asset_test','ownership_snapshot_test','throw_math_test','inventory_transactions_test','armor_equipment_test','resource_economy_test','recipe_registry_test','inventory_mode_test','item_survival_hooks_test','human_motion_camera_test']
EXTENDED_TARGETS=['creature_draw_queue_test','creature_draw_clip_bounds_test','influence_hit_math_test','tool_capabilities_test','influence_use_test','creature_influence_channels_test','creature_equipment_fit_test','fog_range_smoother_test','seam_absent_neighbor_test','mesh_publish_contract_test','miss_first_mesh_class_test','column_flow_scheduler_test','worldgen_vegetation_placement_test','worldgen_fluid_vegetation_pipeline_test']

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--skip-build',action='store_true')
    parser.add_argument('--extended',action='store_true',help='Also build and run regressions for affected streaming/worldgen/render contracts')
    parser.add_argument('--world',default='ItemsCompletionQA')
    args=parser.parse_args()
    if not args.world.endswith('QA') or '/' in args.world or '\\' in args.world:
        parser.error('The fixture mutates terrain: use a dedicated world ending in QA')
    output=ROOT/'bin/items_acceptance'
    output.mkdir(parents=True,exist_ok=True)
    def run(command,name,cwd=ROOT,env=None):
        print(name,flush=True)
        with (output/(name+'.log')).open('w',encoding='utf-8') as log:
            subprocess.run([str(s) for s in command],cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
    targets=ITEM_TARGETS+(EXTENDED_TARGETS if args.extended else [])
    if not args.skip_build:
        run(['cmake','--build','build/desktop-msvc','--config','Release','--target','Cubatarium','--parallel','8'],'desktop-release')
        run(['cmake','--build','build/desktop-msvc','--config','Release','--target',*targets,'--parallel','8'],'unit-release')
    for file in ['test_cpp_audits.py','test_itemgen_publication.py','test_itemgen_mesh.py','test_itemgen_surface_uv.py','test_itemgen_design.py','test_itemgen_workshop.py','test_item_design_probes.py','validate_item_defs.py','audit_item_visuals.py','test_itemgen_raster.py','test_survival_chains.py','research/throw_prototype.py']:
        run([sys.executable,ROOT/'tools'/file],Path(file).stem)
    designs=sorted((ROOT/'tools/item_designs').glob('*/design.yaml'))
    for design in designs:
        run([sys.executable,ROOT/'tools/itemgen.py','build-design','--id',design.parent.name], 'stage-design-'+design.parent.name)
    test_filter=['-R','^('+'|'.join(targets)+')$'] if args.extended else ['-L','items']
    run(['ctest','--test-dir','build/desktop-msvc','-C','Release',*test_filter,'--no-tests=error','--output-on-failure'],'ctest-items')
    if not (ROOT/'bin/worlds'/args.world).is_dir():
        run([ROOT/'bin/Cubatarium.exe','--create-world','--name',args.world,'--seed','13112026','--radius-chunks','1'],'create-qa-world',cwd=ROOT/'bin')
    env=os.environ.copy()
    native=output/'native.json'
    # A successful process must produce this run's reports, never reuse a prior pass.
    for report in [native,Path(str(native)+'.ui.json'),Path(str(native)+'.ownership.json'),Path(str(native)+'.ownership-ui.json'),Path(str(native)+'.player-window.json'),Path(str(native)+'.icons.json'),Path(str(native)+'.item-lab.json'),Path(str(native)+'.presentations.json'),output/'flight.json',output/'result.json']:
        report.unlink(missing_ok=True)
    env.update(PYTHONIOENCODING='utf-8',CUBA_ITEMS_INVENTORY_QA=str(native),CUBA_ITEMS_SURVIVAL_LOOP_QA='1',CUBA_ITEM_LAB_QA=str(native)+'.item-lab.json',CUBA_ITEM_PRESENTATION_QA=str(native)+'.presentations.json',CUBA_FLIGHT_CAPTURE_DIR=str(output/'captures'),CUBA_FLIGHT_CAPTURE_INTERVAL_SEC='4')
    # Native fixtures run before simulated route time. Allow their bounded
    # per-design capture work, rather than treating a growing catalog as a hang.
    timeout=max(240,180+6*len(designs))
    run([sys.executable,ROOT/'tools/flight_sim_run.py','--world',args.world,'--require-existing-world','--seconds','27','--process-timeout',str(timeout),'--no-fly','--no-hold-forward','--idle-sec','2','--report',output/'flight.json'],'native-flight',env=env)
    reports={name:json.loads(path.read_text(encoding='utf-8')) for name,path in [('gameplay',native),('window',Path(str(native)+'.ui.json')),('ownership',Path(str(native)+'.ownership.json')),('ownership_ui',Path(str(native)+'.ownership-ui.json')),('player_window',Path(str(native)+'.player-window.json')),('icons',Path(str(native)+'.icons.json')),('item_lab',Path(str(native)+'.item-lab.json')),('presentations',Path(str(native)+'.presentations.json'))]}
    passed=all(r.get('passed') is True for r in reports.values())
    (output/'result.json').write_text(json.dumps(dict(passed=passed,reports=reports),indent=2)+'\n',encoding='utf-8')
    print(f"passed={passed}; evidence={output}")
    return 0 if passed else 1
if __name__=='__main__':
    raise SystemExit(main())
