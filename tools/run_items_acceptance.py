"""Release-only item/inventory/throw regression runner with native QA fixtures."""
from pathlib import Path
import argparse
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--skip-build',action='store_true')
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
    if not args.skip_build:
        run(['cmake','--build','build/desktop-msvc','--config','Release','--target','Cubatarium','--parallel','8'],'desktop-release')
        run(['cmake','--build','build/desktop-msvc','--config','Release','--target','throw_math_test','inventory_transactions_test','armor_equipment_test','resource_economy_test','recipe_registry_test','inventory_mode_test','item_survival_hooks_test','human_motion_camera_test','--parallel','8'],'unit-release')
    for file in ['validate_item_defs.py','audit_item_visuals.py','test_itemgen_raster.py','test_survival_chains.py','research/throw_prototype.py']:
        run([sys.executable,ROOT/'tools'/file],Path(file).stem)
    run(['ctest','--test-dir','build/desktop-msvc','-C','Release','-L','items','--no-tests=error','--output-on-failure'],'ctest-items')
    if not (ROOT/'bin/worlds'/args.world).is_dir():
        run([ROOT/'bin/Cubatarium.exe','--create-world','--name',args.world,'--seed','13112026','--radius-chunks','1'],'create-qa-world',cwd=ROOT/'bin')
    env=os.environ.copy()
    native=output/'native.json'
    env.update(PYTHONIOENCODING='utf-8',CUBA_ITEMS_INVENTORY_QA=str(native),CUBA_ITEMS_SURVIVAL_LOOP_QA='1',CUBA_FLIGHT_CAPTURE_DIR=str(output/'captures'),CUBA_FLIGHT_CAPTURE_INTERVAL_SEC='4')
    run([sys.executable,ROOT/'tools/flight_sim_run.py','--world',args.world,'--require-existing-world','--seconds','27','--no-fly','--no-hold-forward','--idle-sec','2','--report',output/'flight.json'],'native-flight',env=env)
    reports={name:json.loads(path.read_text(encoding='utf-8')) for name,path in [('gameplay',native),('window',Path(str(native)+'.ui.json'))]}
    passed=all(r.get('passed') is True for r in reports.values())
    (output/'result.json').write_text(json.dumps(dict(passed=passed,reports=reports),indent=2)+'\n',encoding='utf-8')
    print(f"passed={passed}; evidence={output}")
    return 0 if passed else 1
if __name__=='__main__':
    raise SystemExit(main())
