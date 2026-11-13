"""Gate real-input human movement and camera recordings, separately from previews."""
from pathlib import Path
import argparse,collections,hashlib,json,math,statistics

def audit(source,output):
    rows=[json.loads(line)for line in source.read_text(encoding='utf-8').splitlines()if line]
    groups=collections.defaultdict(list)
    for row in rows:groups[row['scenario']].append(row)
    def speed(row):return math.hypot(row['velocity'][0],row['velocity'][2])
    def observed(name,predicate):return any(predicate(r)for r in groups[name])
    checks={
        'walk':observed('walk',lambda r:r['on_ground'] and speed(r)>1),
        'run':observed('run',lambda r:r['on_ground'] and speed(r)>3.2),
        'crouch_steps':observed('crouch_walk',lambda r:r['stance']>.95 and .1<speed(r)<1.8),
        'jump':observed('jump',lambda r:r['mode']==4 and r['velocity'][1]>.5),
        'fall_and_land':observed('fall',lambda r:r['mode']==5 and r['velocity'][1]<-.5) and observed('fall',lambda r:r['landing_events']>0),
        'hover':observed('hover',lambda r:r['mode']==1 and speed(r)<.05 and abs(r['velocity'][1])<.1),
        'fly_up':observed('fly_up',lambda r:r['mode']==1 and r['velocity'][1]>.5),
        'fly_down':observed('fly_down',lambda r:r['mode']==1 and r['velocity'][1]<-.5),
        'fly_forward':observed('fly_forward',lambda r:r['mode']==1 and speed(r)>.5),
        'surface_idle':observed('surface_idle',lambda r:r['swimming'] and not r['head_submerged']),
        'surface_swim':observed('surface_swim',lambda r:r['swimming'] and not r['head_submerged'] and speed(r)>.1),
        'submerged_idle':observed('submerged_idle',lambda r:r['swimming'] and r['head_submerged'] and speed(r)<.1),
        'swim_up':observed('swim_up',lambda r:r['swimming'] and r['velocity'][1]>.5),
        'swim_forward':observed('swim_forward',lambda r:r['swimming'] and r['head_submerged'] and speed(r)>.5),
        'camera_volume':bool(rows) and all(r['camera_clear']for r in rows),
        'camera_interframe':bool(rows) and all(r.get('path_clear',False)for r in rows),
        'both_perspectives':all(len(groups[name])>=10 for name in ('camera_back','camera_front')),
    }
    stages={}
    for name,values in groups.items():
        stages[name]={'samples':len(values),'modes':dict(collections.Counter(str(r['mode'])for r in values)),
                      'horizontal_speed_median':statistics.median(speed(r)for r in values),
                      'vertical_speed_min':min(r['velocity'][1]for r in values),'vertical_speed_max':max(r['velocity'][1]for r in values),
                      'swimming_samples':sum(r['swimming']for r in values),'submerged_samples':sum(r['head_submerged']for r in values),
                      'bad_camera_volumes':sum(not r['camera_clear']for r in values),'bad_camera_paths':sum(not r.get('path_clear',False)for r in values)}
    result={'schema':'human-motion-real-input-route.v1','passed':all(checks.values()),'checks':checks,'samples':len(rows),
            'source':str(source),'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'stages':stages}
    output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'passed':result['passed'],'failed':[k for k,v in checks.items()if not v],'samples':len(rows)}))
    if not result['passed']:raise SystemExit(1)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('--out',type=Path,required=True);a=p.parse_args();audit(a.source,a.out)
