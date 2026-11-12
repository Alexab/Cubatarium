"""Paired actual-world distance gait: three speeds plus dead-zone boundary; not art/terrain acceptance."""
import json
from pathlib import Path
import numpy as np
from audit_creature_badger_support_world import load, support_episodes
from audit_creature_badger_stance import eligible, stats, episodes
from audit_creature_motion_v5 import sha

ROOT=Path(__file__).resolve().parents[2]


def clock_audit(rows,duration,definition,distance):
    previous=None;errors=[];rates=[];frozen_travel=0.;frozen_records=0
    reference=np.float32(max(definition['locomotion']['walk_speed'],.05))
    speed=np.float32(definition['visual']['animation']['clips']['walk']['speed'])
    for row in rows:
        if previous and eligible(row)and eligible(previous):
            dt=row['elapsed_render']-previous['elapsed_render']
            delta=row['clip_time']-previous['clip_time']
            if delta < -duration*.5:delta+=duration
            if 0<dt<=.1 and delta>=0:
                actual=np.float32(row['actual_speed'])
                if distance>0:rate=np.float32(float(actual)*duration/float(np.float32(distance)))
                else:
                    ratio=np.float32(np.clip(actual/reference,0,1.5))
                    ratio=np.float32(0)if ratio<np.float32(.04)else max(np.float32(.22),ratio)
                    rate=np.float32(speed*ratio)
                expected=float(np.float32(np.float32(row['dt'])*rate))
                errors.append(abs(delta-expected));rates.append(delta/dt)
                if delta==0:
                    frozen_records+=1
                    frozen_travel+=float(np.linalg.norm((np.array(row['body_origin'])-np.array(previous['body_origin']))[[0,2]]))
        previous=row
    assert errors and max(errors)<1e-5
    return {'observed_rate':stats(rates),'per_step_clip_delta_error_seconds':stats(errors),
            'moving_records_with_frozen_clock':frozen_records,'body_travel_while_clock_frozen_world':frozen_travel}


def transition_audit(rows):
    fade=[r for r in rows if r['fade_weight']>.001]
    heights=[min(f['lowest_vertex_y']for f in r['feet'])-r['body_origin'][1]for r in fade]
    entries=sum(a['clip']!=b['clip']for a,b in zip(rows,rows[1:]))
    return {'clip_switches':entries,'fade_records':len(fade),'minimum_foot_gap_during_fades':stats(heights),
            'scope':'Only vertical submitted foot envelope at fades; horizontal plant/release and artistic continuity still pending'}


def main():
    out=ROOT/'docs/research/creature-r6-v2/distance-gait-v1';versions={}
    for speed_name,requested,seconds in [('crawl',.08,36),('slow',.2,52),('normal',.5,28),('fast',1.,20)]:
        paired={};contexts={}
        for mode in ['legacy','distance']:
            label=mode+'_'+speed_name;folder=out/'world'/label
            rows,duration,provenance=load(folder,label)
            context=json.loads((folder/f'context_{label}.json').read_text());contexts[mode]=context
            assert context['world_requested_speed']==requested and context['world_seconds']==seconds
            root=Path(context['models_root']);definition=json.loads((root/'creatures/badger/creature.json').read_text())
            distance=definition['visual']['animation']['clips']['walk'].get('ground_distance_per_cycle',0)
            assert (distance>0)==(mode=='distance')
            assert provenance['asset_bin_sha256']=='d9e29c78482eba4823eaea78d85762747325f09afb7f0d25fe35836d477ad6aa'
            motion=[json.loads(line)for line in (folder/f'world_motion_{label}.jsonl').read_text().splitlines()if line]
            assert all(abs(r['requested_speed']-requested)<1e-6 for r in motion if r['requested_move'])
            assert any(not r['requested_move']for r in motion)
            sample=[r for r in rows if eligible(r)]
            support=support_episodes(rows,duration,14,'current')
            if speed_name=='crawl'and mode=='legacy':
                assert support['episodes']==0,'Frozen phase0 should not enter inner stance window'
            else:assert support['episodes']>0
            near_plane=episodes(rows,.001)
            paired[mode]={'provenance':provenance,'requested_speed':requested,'seconds':seconds,
                'steady_records':len(sample),'actual_speed':stats([r['actual_speed']for r in sample]),
                'clock':clock_audit(rows,duration,definition,distance),'support':support,'transitions':transition_audit(rows),
                'near_plane_candidates':{'threshold_world':.001,'scope':'Height-only candidates, not independent plant labels',
                    'episodes':len(near_plane),'ratio':stats([r['displacement_ratio']for r in near_plane])}}
            print(label,'phase ratio',support['foot_over_body_ratio']['p50']if support['foot_over_body_ratio']else None,
                  'near-plane ratio',paired[mode]['near_plane_candidates']['ratio']['p50'],flush=True)
        a,b=contexts['legacy'],contexts['distance']
        assert a['exe_sha256']==b['exe_sha256'] and a['shaders_sha256']==b['shaders_sha256']
        for name,digest in a['files_sha256'].items():
            if name!='creatures\\badger\\creature.json':assert b['files_sha256'][name]==digest
        versions[speed_name]=paired
    report={'scope':__doc__,'art_acceptance':'pending','turn_terrain_acceptance':'pending','performance_acceptance':'not_evaluated',
        'world':'CreatureQaPerf_HintV1','phase_labels':'Both modes use identical predeclared14-stride inner support windows0.05…0.45; no height/speed-based point selection',
        'versions':versions,'analyzer_sha256':sha(Path(__file__))}
    (out/'world_audit.json').write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':main()
