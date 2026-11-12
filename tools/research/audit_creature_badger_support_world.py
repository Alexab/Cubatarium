"""Submitted world support trajectories with predefined phase labels, QA only."""
import json
from pathlib import Path
import numpy as np
from review_creature_redesign import accessor
from audit_creature_motion_v5 import sha
from audit_creature_badger_stance import eligible, episodes, stats

ROOT=Path(__file__).resolve().parents[2]


def load(folder,label):
    context=json.loads((folder/f'context_{label}.json').read_text())
    assert context['foot_probe'] and not context['pass_timing'] and context['world_motion']=='land_walk_pause'
    assert context['exe_sha256']==sha(ROOT/'bin/Cubatarium.exe')
    models=Path(context['models_root'])
    for rel,h in context['files_sha256'].items():assert sha(models/rel)==h
    for rel,h in context['shaders_sha256'].items():assert sha(ROOT/rel)==h
    process=json.loads((folder/f'report_{label}.json').read_text())
    assert process['process_success'] and process['world_startup']['runtime_world']==context['world']
    rows=[json.loads(line)for line in (folder/f'foot_probe_{label}.jsonl').read_text().splitlines()]
    assert all(r['schema']=='creature-qa-foot-world.v3' for r in rows)
    g=json.loads((models/'creatures/badger/model.gltf').read_text())
    blobs=[(models/'creatures/badger'/b['uri']).read_bytes()for b in g['buffers']]
    parents={c:i for i,n in enumerate(g['nodes'])for c in n.get('children',[])}
    joint_names=[g['nodes'][i]['name']for i in g['skins'][0]['joints']]
    # Independently enumerate all vertices wholly weighted to each subtree.
    for foot in rows[0]['feet']:
        root=next(i for i,n in enumerate(g['nodes'])if n['name']==foot['bone'])
        members=[]
        for j,node in enumerate(g['skins'][0]['joints']):
            chain=node
            while True:
                if chain==root:members.append(j);break
                if chain not in parents:break
                chain=parents[chain]
        assert foot['joint_members']==[joint_names[j]for j in members]
        expected=[]
        for mesh in g['meshes']:
            for primitive in mesh['primitives']:
                a=primitive['attributes'];p=accessor(g,blobs,a['POSITION']);j=accessor(g,blobs,a['JOINTS_0']).astype(int);w=accessor(g,blobs,a['WEIGHTS_0'])
                selected=np.sum(np.where(np.isin(j,members),w,0),axis=1)>=.9999
                for pos,indices,weights in zip(p[selected],j[selected],w[selected]):
                    expected.append((pos,{'joints':[joint_names[i]for i in indices],'weights':weights.tolist()}))
        captured=list(zip(foot['bind_surface_points'],foot['surface_skin']))
        assert all(any(np.linalg.norm(np.array(p)-q)<1e-6 and s==skin for p,s in captured)for q,skin in expected)
        assert all(any(np.linalg.norm(np.array(p)-q)<1e-6 and s==skin for q,skin in expected)for p,s in captured)
    binding=[{k:f[k]for k in ['bone','bind_surface_points','surface_skin','joint_members']}for f in rows[0]['feet']]
    for r in rows:
        assert [{k:f[k]for k in ['bone','bind_surface_points','surface_skin','joint_members']}for f in r['feet']]==binding
        assert all(abs(min(p[1]for p in f['surface_points'])-f['lowest_vertex_y'])<1e-5 for f in r['feet'])
    animation=next(a for a in g['animations']if a['name']=='walk')
    counts=[g['accessors'][animation['samplers'][c['sampler']]['input']]['count']for c in animation['channels']
            if g['nodes'][c['target']['node']]['name'].startswith(('upper_','lower_'))and c['target']['path']=='rotation']
    assert len(counts)==8
    if label in ('live','hip'):assert set(counts)=={9 if label=='live'else 113}
    duration=max(float(accessor(g,blobs,s['input'])[-1,0])for s in animation['samplers'])
    motion=[json.loads(line)for line in (folder/f'world_motion_{label}.jsonl').read_text().splitlines()if line]
    assert not any(r['frozen']or r['body_blocked']for r in motion)
    return rows,duration,{'exe_sha256':context['exe_sha256'],'context_sha256':sha(folder/f'context_{label}.json'),
        'log_sha256':sha(folder/f'foot_probe_{label}.jsonl'),'surface_bindings_complete':True,'asset_bin_sha256':sha(models/'creatures/badger/model.bin')}


def support_episodes(rows,duration,cycles,label):
    active={};done=[];heights=[];previous=None
    def finish(key):
        values=active.pop(key)
        if len(values)<3 or values[-1][0]-values[0][0]<.1:return
        body=values[-1][2]-values[0][2];foot=values[-1][1]-values[0][1];travel=np.linalg.norm(body)
        if travel<.025:return
        done.append({'foot':key[0],'point':key[1],'duration':values[-1][0]-values[0][0],
                     'body_displacement':float(travel),'foot_displacement':float(np.linalg.norm(foot)),
                     'ratio':float(np.linalg.norm(foot)/travel),'injected_ratio':float(np.linalg.norm(foot+2*body)/travel)})
    for row in rows:
        valid=eligible(row)
        if previous:
            dt=row['elapsed_render']-previous['elapsed_render']
            dyaw=abs((row['body_yaw']-previous['body_yaw']+180)%360-180)
            valid=valid and 0<dt<=.1 and dyaw/dt<=90
        keys=set()
        if valid:
            for foot in row['feet']:
                offset=0. if foot['bone'].endswith(('fl','br'))else .5
                phase=(row['clip_time']/duration*cycles+offset)%1.
                # Source authored lower-flex tracks swing in first half;
                # current explicitly plants in first half. Fixed inner windows
                # exclude changes of support; no measured foot speed/height labels.
                stance=(.05<=phase<=.45)if label=='current'else(.55<=phase<=.95)
                if not stance:continue
                bottom=min(p[1]for p in foot['bind_surface_points'])
                for i,(bind,point)in enumerate(zip(foot['bind_surface_points'],foot['surface_points'])):
                    if abs(bind[1]-bottom)>1e-6:continue
                    key=(foot['bone'],i);keys.add(key)
                    p=np.array(point);b=np.array(row['body_origin']);heights.append(float(p[1]-b[1]))
                    active.setdefault(key,[]).append((row['elapsed_render'],p[[0,2]],b[[0,2]]))
        for key in list(active):
            if key not in keys:finish(key)
        previous=row
    for key in list(active):finish(key)
    return {'scope':'Predefined inner stance windows on fixed bind-bottom points; correlated episodes, not independent footfalls',
            'episodes':len(done),'displacement_world':stats([r['foot_displacement']for r in done]),
            'foot_over_body_ratio':stats([r['ratio']for r in done]),'gap_to_actor_plane':stats(heights),
            'negative_control_ratio':stats([r['injected_ratio']for r in done]),'records':done}


def main():
    out=ROOT/'docs/research/creature-r6-v2/badger-stance-ik-v2';result={}
    for label,cycles in [('live',1),('hip',14),('current',14)]:
        rows,duration,provenance=load(out/'world'/label,label)
        sample=[r for r in rows if eligible(r)]
        ratio_episodes={str(t):episodes(rows,t)for t in [.01,.03,.05]}
        result[label]={'records':len(rows),'steady_walk_records':len(sample),'duration':duration,'gait_cycles':cycles,
            'provenance':provenance,'actual_speed':stats([r['actual_speed']for r in sample]),
            'phase_support':support_episodes(rows,duration,cycles,label),
            'height_candidates':{t:{'episodes':len(e),'ratio':stats([r['displacement_ratio']for r in e])}for t,e in ratio_episodes.items()}}
        print(label,'phase ratio',result[label]['phase_support']['foot_over_body_ratio'],flush=True)
    proposal=ROOT/'assetgen_output/creatures/badger_stance_ik_v2'
    report=json.loads((proposal/'proposal_report.json').read_text())
    assert json.loads((proposal/'support_contract.json').read_text())['gait_cycles']==14
    assert result['current']['provenance']['asset_bin_sha256']==report['bin_sha256']
    audit={'scope':__doc__,'art_acceptance':'pending','terrain_acceptance':'pending','performance_acceptance':'not_evaluated',
        'world':'CreatureQaPerf_HintV1','cycles_scope':'Known own live1-cycle and hip/current14-cycle templates; explicit IK support_contract, not key-count inference',
        'contract_sha256':sha(proposal/'support_contract.json'),'versions':result,'analyzer_sha256':sha(Path(__file__))}
    (out/'world_support.json').write_text(json.dumps(audit,indent=2)+'\n')


if __name__=='__main__':main()
