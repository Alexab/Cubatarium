"""Run the player's native acceptance against the required Release executable."""
from pathlib import Path
import argparse,json,os,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'docs/research/player-motion-camera-2026-11-13'

def main():
    parser=argparse.ArgumentParser();parser.add_argument('phase',choices=['native','route']);parser.add_argument('--label',default='final')
    args=parser.parse_args();env=os.environ.copy()
    if args.phase=='native':
        ids=sorted(json.loads(p.read_text(encoding='utf-8'))['id']for p in (ROOT/'models/creatures').glob('*/creature.json'))
        assert len(ids)==56,('production reload fixture expects 56 definitions',len(ids))
        env['CUBA_PLAYER_APPEARANCE_QA']='1'
        command=[sys.executable,str(ROOT/'tools/research/run_creature_pilot_qa.py'),'--production','--world','HumanMotionQa_v1',
                 '--species','badger','--preview-species',','.join(ids),'--out',str(OUT),'--label',args.label,
                 '--native-dir','native_after','--fp-matrix','--resource-reload-probe','--save-probe','--seconds','5','--capture-interval','1']
    else:
        raw=OUT/('raw_route_'+args.label)
        env['CUBA_HUMAN_MOTION_QA']=str(raw/'motion.jsonl')
        env['CUBA_FLIGHT_CAPTURE_DIR']=str(raw/'frames')
        env['CUBA_FLIGHT_CAPTURE_INTERVAL_SEC']='0.25';env['CUBA_FLIGHT_CAPTURE_DELAY_SEC']='2'
        command=[sys.executable,str(ROOT/'tools/flight_sim_run.py'),'--world','HumanMotionQa_v1','--require-existing-world',
                 '--seconds','40','--no-fly','--no-hold-forward','--process-timeout','420','--report',str(OUT/('route_'+args.label+'.json'))]
    subprocess.run(command,cwd=ROOT,env=env,check=True)

if __name__=='__main__':main()
