"""Own badger cadence experiment with error-controlled explicit root contact keys."""
import argparse
from copy import deepcopy
import json
from pathlib import Path
import shutil
import sys
import numpy as np
import yaml
from review_creature_redesign import accessor,sample_nodes,world_matrices
from audit_creature_motion_v5 import sha

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from creaturegen.anatomy import build_model
from creaturegen.export_gltf import export_gltf


def adaptive_root(g,blob,name,tolerance=2e-4):
    blobs=[blob];skin=g['skins'][0]
    inverse=accessor(g,blobs,skin['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)
    p=[];j=[];w=[]
    for mesh in g['meshes']:
        for primitive in mesh['primitives']:
            a=primitive['attributes'];p.append(accessor(g,blobs,a['POSITION']))
            j.append(accessor(g,blobs,a['JOINTS_0']).astype(int));w.append(accessor(g,blobs,a['WEIGHTS_0']))
    p=np.concatenate(p);j=np.concatenate(j);w=np.concatenate(w)
    bind_min=float(p[:,1].min());p=np.column_stack([p,np.ones(len(p))])
    animation=next(a for a in g['animations']if a['name']==name)
    times=sorted({float(t)for s in animation['samplers']for t in accessor(g,blobs,s['input'])[:,0]})
    duration=times[-1];cache={}
    def target(t):
        if t not in cache:
            matrices=world_matrices(sample_nodes(g,blobs,name,t/duration))
            palette=np.array([matrices[node]@inverse[i]for i,node in enumerate(skin['joints'])])
            posed=np.einsum('nvab,nb,nv->na',palette[j],p,w)
            cache[t]=bind_min-float(posed[:,1].min())
        return cache[t]
    def inspect(a,b):
        ya,yb=target(a),target(b);probes=[]
        for ratio in [.25,.5,.75]:
            t=float(np.float32(a+(b-a)*ratio))
            if a<t<b:
                interpolated=ya+(yb-ya)*(t-a)/(b-a)
                probes.append((abs(target(t)-interpolated),t))
        return max(probes,default=(0.,a))
    # Refine all currently failing segments. Quarter probes include contact
    # switches; independent dense validation is still required after export.
    for iteration in range(20):
        added=[]
        for a,b in zip(times,times[1:]):
            error,t=inspect(a,b)
            if error>tolerance:added.append(t)
        if not added:break
        times=sorted(set(times+added))
        if len(times)>2049:raise ValueError(f'{name}: adaptive root exceeded2049-key budget')
    else:raise ValueError('Root refinement did not converge')
    # Validate between fitted keys independently, then lift by the measured
    # worst undershoot plus a small margin. Acceptance floor bound is unchanged.
    dense=sorted(set(float(np.float32(duration*i/8192))for i in range(8193)))
    exact=np.array([target(t)for t in dense]);approx=np.interp(dense,times,[target(t)for t in times])
    margin=max(0.,float(np.max(exact-approx)))+1e-5
    values=[[0.,target(t)+margin,0.]for t in times]
    seam=abs(values[0][1]-values[-1][1]);assert seam<1e-6
    values[-1]=deepcopy(values[0])
    return times,values,{'keys':len(times),'evaluations':len(cache),'tolerance_model_units':tolerance,
        'maximum_quarter_probe_error':max(inspect(a,b)[0]for a,b in zip(times,times[1:])),
        'dense_validation_samples':len(dense),'measured_safety_lift_model_units':margin,
        'dense_floor_low_model_units':float((approx+margin-exact).min()),
        'dense_floor_high_model_units':float((approx+margin-exact).max()),
        'root_y_seam_before_snap':seam,'sampled_not_analytic':True}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qa-name',required=True)
    args=parser.parse_args();assert Path(args.qa_name).name==args.qa_name
    out=ROOT/'assetgen_output/creatures'/args.qa_name;assert not out.exists()
    source=ROOT/'assetgen_output/creatures/badger_sway_v2'
    source_recipe=source/'recipes/badger.yaml'
    recipe=yaml.safe_load(source_recipe.read_text(encoding='utf8'))
    original=yaml.safe_load((ROOT/'tools/creature_recipes/badger.yaml').read_text(encoding='utf8'))
    before=json.loads((source/'models/creatures/badger/model.gltf').read_text())
    names=[a['name']for a in before['animations']]
    check,data=export_gltf(build_model(recipe),names)
    assert check==before and data==(source/'models/creatures/badger/model.bin').read_bytes()
    changed=deepcopy(recipe);edits=[]
    # The observed route provides Walk evidence. Do not change Run cadence
    # without a matching Run capture and contact/load budget of its own.
    for name in ['walk']:
        clip=changed['motion']['clips'][name]
        duration=max(t['times'][-1]for t in clip['tracks'])
        cycles=clip['gait_cycles_per_loop']*2
        for track in clip['tracks']:
            if track['bone'].startswith(('upper_','lower_')):
                old=next(t for t in original['motion']['clips'][name]['tracks']if (t['bone'],t['path'])==(track['bone'],track['path']))
                old_duration=old['times'][-1]
                track['times']=[round(duration*(cycle+t/old_duration)/cycles,8)for cycle in range(cycles)for i,t in enumerate(old['times'])if cycle==0 or i>0]
                track['values']=[deepcopy(v)for cycle in range(cycles)for i,v in enumerate(old['values'])if cycle==0 or i>0]
        root=next(t for t in clip['tracks']if t['bone']=='body' and t['path']=='translation')
        root['times']=[0.,duration];root['values']=[[0.,0.,0.],[0.,0.,0.]]
        clip.pop('contact')
        clip['gait_cycles_per_loop']=cycles
        clip['ground_contact_baked']={'root':'body','method':'adaptive_full_skinned_mesh','tolerance_model_units':2e-4}
        edits.append({'clip':name,'gait_cycles':cycles,'macroloop_asset_seconds':duration,
                      'cadence_multiplier':2.,'body_head_tail_spine_motion_preserved':True})
    draft,blob=export_gltf(build_model(changed),names)
    roots={}
    for name in ['walk']:
        times,values,metrics=adaptive_root(draft,blob,name)
        root=next(t for t in changed['motion']['clips'][name]['tracks']if t['bone']=='body' and t['path']=='translation')
        root.update(times=times,values=values);roots[name]=metrics
        print(name,'adaptive root',metrics,flush=True)
    after,blob=export_gltf(build_model(changed),names)
    assert before['nodes']==after['nodes'] and before['skins']==after['skins']
    out.mkdir(parents=True)
    for kind in ['creatures','skins']:shutil.copytree(source/'models'/kind,out/'models'/kind)
    folder=out/'models/creatures/badger'
    (folder/'model.gltf').write_text(json.dumps(after,indent=2)+'\n')
    (folder/'model.bin').write_bytes(blob)
    (out/'recipes').mkdir();(out/'recipes/badger.yaml').write_text(yaml.safe_dump(changed,sort_keys=False),encoding='utf8')
    report={'state':'unapproved_cadence_experiment','scope':'Walk leg phase shapes retained but elapsed cadence intentionally2x; Run/Idle/Action and slow body/head/tail/spine unchanged; explicit adaptive root correction, not accepted footplants',
        'builder_sha256':sha(Path(__file__)),'source_recipe_sha256':sha(source_recipe),'edits':edits,
        'root_fit':roots,'bin_bytes':len(blob),'source_bin_bytes':len(data),
        'recipe_sha256':sha(out/'recipes/badger.yaml'),'gltf_sha256':sha(folder/'model.gltf'),'bin_sha256':sha(folder/'model.bin')}
    (out/'proposal_report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2),flush=True)


if __name__=='__main__':main()
