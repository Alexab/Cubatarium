"""Scratch rig experiment: preserve upper-body sway, separate hip lean.

No IK or contact acceptance. Additional controls are identity outside Walk;
existing bones and bind geometry are retained, upper legs get control parents.
"""
from copy import deepcopy
import json
from pathlib import Path
import shutil
import numpy as np
import yaml
from scipy.spatial.transform import Rotation
from audit_creature_motion_v5 import inspect, sha
from review_creature_redesign import accessor, sample_nodes, world_matrices
from build_creature_badger_stride_pilot import adaptive_root
from build_creature_badger_ankle_control import geometry
from creaturegen.anatomy import build_model
from creaturegen.export_gltf import export_gltf

ROOT = Path(__file__).resolve().parents[2]


def main():
    source = ROOT / 'assetgen_output/creatures/badger_ankle_control_v1'
    out = ROOT / 'assetgen_output/creatures/badger_hip_control_v1'
    assert not out.exists()
    recipe = yaml.safe_load((source/'recipes/badger.yaml').read_text(encoding='utf8'))
    changed = deepcopy(recipe); bones = changed['anatomy']['bones']
    by_name = {b['name']:b for b in bones}
    for control, parent, suffixes in [('fore_hip_control','body',['fl','fr']), ('rear_hip_control','spine',['bl','br'])]:
        bones.append({'name':control,'parent':parent,'pivot':deepcopy(by_name[parent]['pivot']),'cubes':[]})
        for suffix in suffixes:
            by_name['upper_'+suffix]['parent'] = control
    g, blobs, _ = inspect(source/'models/creatures/badger')
    nodes = {n['name']:i for i,n in enumerate(g['nodes'])}
    animation = next(a for a in g['animations'] if a['name']=='walk')
    duration = max(float(accessor(g,blobs,s['input'])[-1,0]) for s in animation['samplers'])
    # Actual exported parent keys plus a regular refinement grid. This is a
    # sampled controller fit, not an exact quaternion identity at all times.
    times = sorted(set(float(t) for c in animation['channels'] if c['target']['node'] in [nodes['body'],nodes['spine']] and c['target']['path']=='rotation' for t in accessor(g,blobs,animation['samplers'][c['sampler']]['input'])[:,0]) | set(float(np.float32(t)) for t in np.linspace(0,duration,257)))
    values = {name:[] for name in ['fore_hip_control','rear_hip_control']}
    for t in times:
        sampled = sample_nodes(g,blobs,'walk',t/duration)
        world = world_matrices(sampled)
        body_rotation = world[nodes['body']][:3,:3]
        roll = Rotation.from_matrix(body_rotation).as_euler('xyz')[2]
        desired_body = Rotation.from_euler('z',roll*.4).as_matrix()
        spine_local = Rotation.from_quat(sampled[nodes['spine']]['rotation']).as_matrix()
        for control,parent,desired in [('fore_hip_control','body',desired_body), ('rear_hip_control','spine',desired_body@spine_local)]:
            local = world[nodes[parent]][:3,:3].T@desired
            values[control].append(Rotation.from_matrix(local).as_euler('xyz',degrees=True).tolist())
    walk = changed['motion']['clips']['walk']
    for control in values:
        values[control][-1] = deepcopy(values[control][0])
        walk['tracks'].append({'bone':control,'path':'rotation','times':times,'values':values[control]})
    root = next(t for t in walk['tracks'] if t['bone']=='body' and t['path']=='translation')
    root.update(times=[0.,duration],values=[[0.,0.,0.],[0.,0.,0.]])
    names = [a['name'] for a in g['animations']]
    draft, blob = export_gltf(build_model(changed),names)
    root_times,root_values,fit = adaptive_root(draft,blob,'walk')
    root.update(times=root_times,values=root_values)
    after,blob = export_gltf(build_model(changed),names)
    assert len(after['skins'][0]['joints'])==19
    max_other_error = max(float(np.max(np.abs(geometry(g,blobs,clip,float(p))-geometry(after,[blob],clip,float(p))))) for clip in [None,'idle','run','punch'] for p in np.linspace(0,1,65 if clip else 1))
    assert max_other_error<1e-7
    # Bind-position/UV equality, decoded independently of accessor offsets.
    for ma,mb in zip(g['meshes'],after['meshes']):
        for pa,pb in zip(ma['primitives'],mb['primitives']):
            for name in ['POSITION','TEXCOORD_0']:
                assert np.array_equal(accessor(g,blobs,pa['attributes'][name]),accessor(after,[blob],pb['attributes'][name]))
            assert np.array_equal(accessor(g,blobs,pa['indices']),accessor(after,[blob],pb['indices']))
    out.mkdir(parents=True);shutil.copytree(source/'models',out/'models')
    folder=out/'models/creatures/badger'
    (folder/'model.gltf').write_text(json.dumps(after,indent=2)+'\n');(folder/'model.bin').write_bytes(blob)
    (out/'recipes').mkdir();(out/'recipes/badger.yaml').write_text(yaml.safe_dump(changed,sort_keys=False),encoding='utf8')
    for p in (out/'models/creatures').glob('*/creature.json'):
        d=json.loads(p.read_text(encoding='utf8'));d['natural_spawnable']=False;p.write_text(json.dumps(d,indent=2,ensure_ascii=False)+'\n',encoding='utf8')
    report={'scope':__doc__,'state':'unapproved_decoupled_hip_experiment','joints':19,
            'upper_body_roll_degrees':20,'target_hip_roll_degrees':8,'controller_keys':len(times),
            'root_fit':fit,'max_other_clip_vertex_error_model':max_other_error,'bin_bytes':len(blob),
            'source_report_sha256':sha(source/'proposal_report.json'),'builder_sha256':sha(Path(__file__)),
            'proposal_sha256':{p.name:sha(p) for p in [folder/'model.gltf',folder/'model.bin',out/'recipes/badger.yaml']}}
    (out/'proposal_report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2),flush=True)


if __name__=='__main__':
    main()
