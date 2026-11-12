"""Analyze actual submitted foot surface trajectories; no automatic footplant acceptance."""
import argparse
from copy import deepcopy
from pathlib import Path
import hashlib
import json
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from review_creature_redesign import accessor

ROOT=Path(__file__).resolve().parents[2]


def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def stats(values):
    a=np.asarray(values,float)
    return None if not len(a) else {'n':len(a),'p50':float(np.median(a)),
        'p95':float(np.quantile(a,.95)),'min':float(a.min()),'max':float(a.max())}


def load(out,label):
    context=json.loads((out/f'context_{label}.json').read_text())
    assert context['foot_probe'] and not context['pass_timing']
    assert context['world_motion']=='land_walk_pause' and context['world_species']==['badger']
    assert context['exe_sha256']==sha(ROOT/'bin/Cubatarium.exe')
    models=Path(context['models_root'])
    for rel,digest in context['files_sha256'].items(): assert sha(models/rel)==digest
    for rel,digest in context['shaders_sha256'].items(): assert sha(ROOT/rel)==digest
    process=json.loads((out/f'report_{label}.json').read_text())
    assert process['process_success'] and process['world_startup']['runtime_world']==context['world']
    file=out/f'foot_probe_{label}.jsonl'
    rows=[json.loads(line)for line in file.read_text().splitlines()if line]
    assert len(rows)>100 and {r['schema']for r in rows}=={'creature-qa-foot-world.v2'}
    assert all(r['on_ground'] and not r['in_fluid'] for r in rows)
    binding={f['bone']:f['bind_surface_points']for f in rows[0]['feet']}
    error=0.
    for r in rows:
        assert {f['bone']:f['bind_surface_points']for f in r['feet']}==binding
        for f in r['feet']:
            points=np.asarray(f['surface_points'])
            assert np.isfinite(points).all()
            error=max(error,abs(float(points[:,1].min())-f['lowest_vertex_y']))
    assert error<1e-5,'Probe omitted a true foot surface extreme'
    folder=models/'creatures/badger';g=json.loads((folder/'model.gltf').read_text())
    blobs=[(folder/b['uri']).read_bytes()for b in g['buffers']]
    durations={a['name']:max(float(accessor(g,blobs,s['input'])[-1,0])for s in a['samplers'])for a in g['animations']}
    walk=next(a for a in g['animations']if a['name']=='walk')
    cycle_counts=[]
    for c in walk['channels']:
        if g['nodes'][c['target']['node']]['name'].startswith(('upper_','lower_')) and c['target']['path']=='rotation':
            count=g['accessors'][walk['samplers'][c['sampler']]['input']]['count']
            assert (count-1)%8==0
            cycle_counts.append((count-1)//8)
    assert len(cycle_counts)==8 and len(set(cycle_counts))==1
    definition=json.loads((folder/'creature.json').read_text())
    world=[json.loads(line)for line in (out/f'world_motion_{label}.jsonl').read_text().splitlines()if line]
    assert not any(r['frozen'] or r['body_blocked'] for r in world)
    return rows,{'context_sha256':sha(out/f'context_{label}.json'),'log_sha256':sha(file),
        'actual_surface_min_error':error,'durations':durations,'walk_leg_cycles':cycle_counts[0],
        'cycle_inference_scope':'Own badger8-key-interval stride template, all8 leg channels agree; not generic glTF inference',
        'walk_speed_reference':definition['locomotion']['walk_speed'],'binding':binding,
        'exe_sha256':context['exe_sha256']}


def eligible(row):
    return row['elapsed_render']>=2 and row['state'].lower()=='walk' and row['clip']=='walk' and row['fade_weight']<=.001 and row['on_ground'] and not row['in_fluid'] and row['requested_move'] and row['actual_speed']>.05


def episodes(rows,tolerance):
    active={};completed=[];previous=None
    def finish(key):
        track=active.pop(key)
        if len(track)<3:return
        duration=track[-1][0]-track[0][0]
        if duration<.06:return
        foot=np.asarray([x[1]for x in track])[:,[0,2]]
        body=np.asarray([x[2]for x in track])[:,[0,2]]
        b=body[-1]-body[0];f=foot[-1]-foot[0]
        bd=float(np.linalg.norm(b));fd=float(np.linalg.norm(f))
        if bd<.025:return
        relative=f-b;backward=float(relative@b)<0
        gain=-float(b@relative)/max(float(relative@relative),1e-12) if backward else None
        completed.append({'foot':key[0],'point':key[1],'start':track[0][0],'end':track[-1][0],
            'duration':duration,'world_foot_displacement':fd,'body_displacement':bd,
            'displacement_ratio':fd/bd,'backward_relative_motion':backward,'linear_cadence_fit_hint':gain})
    for row in rows:
        dt=row['elapsed_render']-previous['elapsed_render'] if previous else 0
        yaw=abs((row['body_yaw']-previous['body_yaw']+180)%360-180) if previous else 0
        valid=eligible(row) and 0<dt<=.1 and yaw/dt<=90
        present=set()
        if valid:
            for foot in row['feet']:
                for i,point in enumerate(foot['surface_points']):
                    key=(foot['bone'],i)
                    if point[1]-row['body_origin'][1]<=tolerance:
                        present.add(key);active.setdefault(key,[]).append((row['elapsed_render'],point,row['body_origin']))
        for key in list(active):
            if key not in present:finish(key)
        previous=row
    for key in list(active):finish(key)
    return completed


def summarize(rows,durations,cycles):
    walking=[r for r in rows if eligible(r)];assert len(walking)>100
    rate=[]
    for a,b in zip(rows,rows[1:]):
        if not eligible(a) or not eligible(b):continue
        dt=b['elapsed_render']-a['elapsed_render']
        delta=b['clip_time']-a['clip_time']
        if delta<0:delta+=durations['walk']
        if 0<dt<.1:rate.append(delta/dt)
    stride=[durations['walk']/cycles/v for v in rate if v>1e-6]
    result={'records':len(rows),'steady_walk_records':len(walking),
        'actual_speed':stats([r['actual_speed']for r in walking]),'actual_clip_rate':stats(rate),
        'actual_stride_seconds':stats(stride),'height_threshold_sweep':{}}
    for tol in [.01,.03,.05]:
        e=episodes(rows,tol)
        result['height_threshold_sweep'][str(tol)]={'point_episodes':len(e),
            'world_foot_displacement_blocks':stats([x['world_foot_displacement']for x in e]),
            'foot_over_body_displacement_ratio':stats([x['displacement_ratio']for x in e]),
            'linear_cadence_fit_hint':stats([x['linear_cadence_fit_hint']for x in e if x['linear_cadence_fit_hint'] is not None]),
            'episodes':e}
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();out=args.out.resolve()
    data={};provenance={};report={}
    for label in ['before','current']:
        data[label],provenance[label]=load(out/label,label)
        report[label]=summarize(data[label],provenance[label]['durations'],provenance[label]['walk_leg_cycles'])
    assert provenance['before']['binding']==provenance['current']['binding']
    # An actual-trajectory negative control: introduce root-aligned XZ drift
    # without changing heights, frame facts or contact-candidate classification.
    changed=deepcopy(data['current']);initial=np.array(changed[0]['body_origin'])
    for row in changed:
        shift=2*(np.array(row['body_origin'])-initial)
        for foot in row['feet']:
            for point in foot['surface_points']:
                point[0]+=float(shift[0]);point[2]+=float(shift[2])
    original=report['current']['height_threshold_sweep']['0.03']['foot_over_body_displacement_ratio']['p50']
    injected=stats([x['displacement_ratio']for x in episodes(changed,.03)])['p50']
    assert injected>original+1.,'Analyzer did not detect added planar drift'
    fig,axes=plt.subplots(2,2,figsize=(13,8),layout='constrained')
    for col,label in enumerate(['before','current']):
        rows=[r for r in data[label]if 2<=r['elapsed_render']<=10]
        for i,name in enumerate(['lower_fl','lower_fr','lower_bl','lower_br']):
            axes[0,col].plot([r['elapsed_render']for r in rows],
                [r['feet'][i]['lowest_vertex_y']-r['body_origin'][1]for r in rows],label=name,linewidth=.9)
        axes[0,col].set(title=label+' | actual foot surface gap',xlabel='Render elapsed [s]',ylabel='Height above actor feet plane [blocks]')
        axes[0,col].axhline(.03,color='black',linestyle='--',linewidth=.7);axes[0,col].legend(fontsize=8,ncol=2)
    ax=axes[1,0]
    for offset,label,color in [(-.15,'before','#4c78a8'),(.15,'current','#f58518')]:
        values=[[x['displacement_ratio']for x in report[label]['height_threshold_sweep'][str(t)]['episodes']]for t in [.01,.03,.05]]
        boxes=ax.boxplot(values,positions=np.arange(3)+offset,widths=.25,showfliers=False,patch_artist=True)
        for patch in boxes['boxes']:patch.set_facecolor(color)
    ax.set(xticks=np.arange(3),xticklabels=['0.01','0.03','0.05'],xlabel='Height-only contact-candidate threshold [blocks]',
           ylabel='Fixed point / actor XZ displacement',title='Point episodes >=0.06s; height is not a footplant label')
    ax.axhline(0,color='black',linewidth=.7)
    axes[1,1].bar(['before','current'],[report[l]['actual_stride_seconds']['p50']for l in ['before','current']],
                  color=['#4c78a8','#f58518'])
    axes[1,1].axhline(.35,color='red',linestyle='--',label='unit-travel stride 0.35s')
    axes[1,1].set(ylabel='Observed Walk stride period [s]',title='Actual playback differs from unit-travel preview')
    axes[1,1].legend(fontsize=8)
    fig.suptitle('Badger: actual submitted skin/root observation — not artistic or terrain acceptance')
    fig.savefig(out/'stance_diagnostics.png',dpi=140);plt.close(fig)
    result={'scope':'CPU submitted skin/root foot surface point trajectories in one flat-ground ordinary-motor route; geometric contact candidates, not certified footplants or GPU/terrain/art/performance acceptance',
        'evidence_consistent':True,'art_acceptance':'pending','stance_acceptance':'pending','provenance':provenance,
        'negative_control':{'added_root_aligned_drift_detected':True,'original_ratio_p50':original,'injected_ratio_p50':injected},
        'versions':report,'analyzer_sha256':sha(Path(__file__))}
    (out/'stance_audit.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:{x:v[x] for x in ['records','steady_walk_records','actual_clip_rate','actual_stride_seconds']}for k,v in report.items()},indent=2))


if __name__=='__main__':main()
