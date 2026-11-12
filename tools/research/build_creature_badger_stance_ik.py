"""Unpublished constrained badger Walk bake; not a runtime foot-lock solver.

Predefined diagonal support windows, fixed lengths, world-oriented foot goals.
Nominal travel is derived from the definition, not fitted to observed foot drift.
All other clips and the slow upper-body motion remain source-authored.
"""
import argparse
from copy import deepcopy
import json
from pathlib import Path
import shutil
import numpy as np
from scipy.spatial.transform import Rotation
import yaml
from audit_creature_motion_v5 import inspect, sha
from review_creature_redesign import accessor, sample_nodes, world_matrices
from build_creature_badger_ankle_control import geometry
from creaturegen.anatomy import build_model
from creaturegen.export_gltf import export_gltf

ROOT=Path(__file__).resolve().parents[2]


def unit(v):
    length=np.linalg.norm(v)
    assert length>1e-9
    return v/length


def solve_chain(hip, ankle, l1, l2):
    delta=ankle-hip;distance=np.linalg.norm(delta);direction=unit(delta)
    assert abs(l1-l2)<distance<l1+l2
    pole=np.array([0.,0.,-1.]);pole=unit(pole-direction*np.dot(pole,direction))
    along=(l1*l1-l2*l2+distance*distance)/(2*distance)
    knee=hip+direction*along+pole*np.sqrt(max(0.,l1*l1-along*along))
    first=unit(knee-hip);second=unit(ankle-knee)
    x=-unit(np.cross(first,second))
    upper=np.column_stack([x,-first,np.cross(x,-first)])
    lower=np.column_stack([x,-second,np.cross(x,-second)])
    assert np.linalg.norm(hip+upper@np.array([0.,-l1,0.])+lower@np.array([0.,-l2,0.])-ankle)<1e-8
    knee_angle=float(np.degrees(np.arccos(np.clip(first@second,-1,1))))
    return upper,lower,knee_angle


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qa-name',default='badger_stance_ik_v1')
    parser.add_argument('--samples',type=int,default=1025)
    parser.add_argument('--rigid-paws',action='store_true',help='Keep each named foot block and its claws rigid on the new foot joint')
    args=parser.parse_args();assert Path(args.qa_name).name==args.qa_name
    assert args.samples in [513,1025,2049]
    source=ROOT/'assetgen_output/creatures/badger_hip_control_v1'
    out=ROOT/'assetgen_output/creatures'/args.qa_name;assert not out.exists()
    folder=source/'models/creatures/badger';g,blobs,_=inspect(folder)
    recipe=yaml.safe_load((source/'recipes/badger.yaml').read_text(encoding='utf8'))
    changed=deepcopy(recipe);walk=changed['motion']['clips']['walk']
    if args.rigid_paws:
        for bone in changed['anatomy']['bones']:
            if bone['name'].startswith('lower_'):
                for cube in bone['cubes']:
                    cube['blend']={'bone':'foot_'+bone['name'].split('_',1)[1],
                                   'axis':'y','range':[0,3],'weights':[1,1]}
    definition=json.loads((folder/'creature.json').read_text(encoding='utf8'))
    scale=definition['visual']['gltf']['model_scale']
    offset=definition['visual']['gltf'].get('model_offset_y',0);assert offset==0
    speed=definition['visual']['animation']['clips']['walk']['speed']
    travel=definition['locomotion']['walk_speed']/(scale*speed)
    cycles=walk['gait_cycles_per_loop'];assert cycles==14
    animation=next(a for a in g['animations'] if a['name']=='walk')
    duration=max(float(accessor(g,blobs,s['input'])[-1,0])for s in animation['samplers'])
    step=travel*duration/cycles
    nodes={n['name']:i for i,n in enumerate(g['nodes'])}
    parents={c:i for i,n in enumerate(g['nodes'])for c in n.get('children',[])}
    bind=world_matrices(g['nodes']);joint_names=[g['nodes'][i]['name']for i in g['skins'][0]['joints']]
    primitive=g['meshes'][0]['primitives'][0];attrs=primitive['attributes']
    positions=accessor(g,blobs,attrs['POSITION']);joints=accessor(g,blobs,attrs['JOINTS_0']).astype(int)
    weights=accessor(g,blobs,attrs['WEIGHTS_0'])
    specs={}
    for suffix in ['fl','fr','bl','br']:
        upper,lower,foot=[nodes[p+'_'+suffix]for p in ['upper','lower','foot']]
        l1=float(np.linalg.norm(g['nodes'][lower]['translation']));l2=float(np.linalg.norm(g['nodes'][foot]['translation']))
        foot_origin=bind[foot][:3,3];sole_local=np.array([0.,-foot_origin[1],.5/16])
        mask=np.any((joints==joint_names.index('foot_'+suffix))&(weights>.9999),axis=1)
        rigid=positions[mask]-foot_origin;assert len(rigid)>3
        specs[suffix]={'upper':upper,'lower':lower,'foot':foot,'l1':l1,'l2':l2,
            'sole_local':sole_local,'sole_bind':foot_origin+sole_local,'rigid':rigid,
            'phase_offset':0. if suffix in ['fl','br']else .5}
    contract={'schema':'badger-support-contract.v1','scope':'Predefined flat middle-stance pilot; not approved biological gait or world acceptance',
        'gait_cycles':cycles,'stance_duty':.5,'phase_offsets':{k:v['phase_offset']for k,v in specs.items()},
        'travel_model_per_clip_second':travel,'stride_model':step,'nominal_walk_speed_world':definition['locomotion']['walk_speed'],
        'runtime_speed_floor_caveat':'Existing travelScale minimum0.22 is not changed; below nominal0.484world/s perfect distance sync is not claimed',
        'swing_lift_model':.06,'swing_pitch_degrees':15,'root_preference_model':.345,
        'knee_limits_degrees':{'stance':[5,120],'swing':[5,155]},'pole_world_model':[0,0,-1],
        'foot_orientation':'horizontal fixed yaw in stance; smooth pitch in swing',
        'hip_foot_angle_limits':'not yet vetted; measure before acceptance',
        'foot_binding':'rigid whole named paw and claws' if args.rigid_paws else 'rejected initial blended foot block'}

    def targets(phase):
        result={}
        for suffix,spec in specs.items():
            p=(phase*cycles+spec['phase_offset'])%1.;stance=p<.5
            if stance:z=step*(.25-p);lift=0.;pitch=0.
            else:
                u=(p-.5)*2
                z=step*(-.25+3*u*u-2*u*u*u-.5*u)
                lift=.06*np.sin(np.pi*u)**2
                pitch=15*np.sin(2*np.pi*u)*np.sin(np.pi*u)**2
            rotation=Rotation.from_euler('x',pitch,degrees=True).as_matrix()
            sole=spec['sole_bind'].copy();sole[2]+=z
            ankle=sole-rotation@spec['sole_local']
            ankle[1]=lift-float((spec['rigid']@rotation.T)[:,1].min())
            result[suffix]={'phase':p,'stance':stance,'ankle':ankle,'rotation':rotation}
        return result

    times=[float(np.float32(t))for t in np.linspace(0,duration,args.samples)]
    values={p+'_'+s:[]for s in specs for p in ['upper','lower','foot']};roots=[];solved=[]
    for index,t in enumerate(times):
        phase=t/duration;goals=targets(phase)
        sampled=sample_nodes(g,blobs,'walk',phase);sampled[nodes['body']]['translation']=[0.,0.,0.]
        world=world_matrices(sampled);intervals=[];hips={}
        for suffix,spec in specs.items():
            parent=world[parents[spec['upper']]]
            hip=(parent@np.array([*g['nodes'][spec['upper']]['translation'],1.]))[:3];hips[suffix]=hip
            goal=goals[suffix];ankle=goal['ankle'];l1,l2=spec['l1'],spec['l2']
            h2=float(np.sum((ankle[[0,2]]-hip[[0,2]])**2))
            maximum=np.sqrt(l1*l1+l2*l2+2*l1*l2*np.cos(np.radians(5)))
            minimum=np.sqrt(l1*l1+l2*l2+2*l1*l2*np.cos(np.radians(120 if goal['stance'] else 155)))
            assert h2<maximum**2,(phase,suffix,'horizontal reach')
            intervals.append((ankle[1]-hip[1]+np.sqrt(max(0.,minimum**2-h2)),ankle[1]-hip[1]+np.sqrt(maximum**2-h2)))
        low=max(x[0]for x in intervals);high=min(x[1]for x in intervals)
        assert high-low>.001,(phase,'empty root interval',low,high)
        root=float(np.clip(.345,low+.0005,high-.0005));roots.append([0.,root,0.])
        angles={}
        for suffix,spec in specs.items():
            hip=hips[suffix].copy();hip[1]+=root;goal=goals[suffix]
            upper,lower,knee=solve_chain(hip,goal['ankle'],spec['l1'],spec['l2'])
            parent=world[parents[spec['upper']]][:3,:3]
            rotations=[parent.T@upper,upper.T@lower,lower.T@goal['rotation']]
            for prefix,matrix in zip(['upper','lower','foot'],rotations):
                values[prefix+'_'+suffix].append(Rotation.from_matrix(matrix).as_euler('xyz',degrees=True).tolist())
            assert 5-1e-5<=knee<=(120 if goal['stance'] else 155)+1e-5
            angles[suffix]={'stance':goal['stance'],'knee_degrees':knee,'foot_local_angle_degrees':float(np.degrees(Rotation.from_matrix(rotations[-1]).magnitude()))}
        solved.append({'phase':phase,'root_interval':[low,high],'root_y':root,'legs':angles})
    for name,track_values in values.items():
        assert np.max(np.abs(Rotation.from_euler('xyz',track_values[0],degrees=True).as_matrix()-Rotation.from_euler('xyz',track_values[-1],degrees=True).as_matrix()))<1e-5
        track_values[-1]=deepcopy(track_values[0])
        track=next((x for x in walk['tracks']if x['bone']==name and x['path']=='rotation'),None)
        if track is None:track={'bone':name,'path':'rotation'};walk['tracks'].append(track)
        track.update(times=times,values=track_values)
    roots[-1]=deepcopy(roots[0])
    root_track=next(x for x in walk['tracks']if x['bone']=='body'and x['path']=='translation')
    root_track.update(times=times,values=roots)
    walk['ground_contact_baked']={'method':'constrained_chain_sampled_root','samples':args.samples}
    walk['gait_support_contract']=contract
    names=[a['name']for a in g['animations']]
    model=build_model(changed)
    after,blob=export_gltf(model,names)
    out.mkdir(parents=True);shutil.copytree(source/'models',out/'models')
    destination=out/'models/creatures/badger'
    (destination/'model.gltf').write_text(json.dumps(after,indent=2)+'\n');(destination/'model.bin').write_bytes(blob)
    (out/'recipes').mkdir();(out/'recipes/badger.yaml').write_text(yaml.safe_dump(changed,sort_keys=False),encoding='utf8')
    (out/'support_contract.json').write_text(json.dumps(contract,indent=2)+'\n')
    print('Solved',args.samples,'poses; validating exported skin floor',flush=True)
    floor=[];bind_y=positions[:,1].min();worst=None
    cube_names=[c.name for b in model.bones for c in b.cubes]
    for i,p in enumerate(np.linspace(0,1,8193)):
        posed=geometry(after,[blob],'walk',float(p));vertex=int(posed[:,1].argmin())
        value=float(posed[vertex,1]-bind_y);floor.append(value)
        if worst is None or value<worst['y_model']:
            worst={'phase':float(p),'vertex':vertex,'cube':cube_names[vertex//24],'y_model':value}
        if i and i%2048==0:print('floor samples',i,flush=True)
        if value*scale<-.005:break
    # A measured global micro lift avoids sparse-key undershoot; a large lift
    # rejects the fit rather than hiding missing support under the whole mesh.
    margin=max(0.,-min(floor))+1e-5
    if margin*scale>=.001:
        rejected={'state':'rejected_mesh_penetration','scope':__doc__,'floor_samples_evaluated':len(floor),
            'worst':worst,'required_global_lift_world':margin*scale,'bin_sha256':sha(destination/'model.bin'),
            'builder_sha256':sha(Path(__file__)),'contract_sha256':sha(out/'support_contract.json')}
        (out/'rejection.json').write_text(json.dumps(rejected,indent=2)+'\n');print(json.dumps(rejected,indent=2),flush=True)
        raise ValueError('Mesh penetration rejects this bake; no compensating large root lift')
    assert (max(floor)+margin)*scale<.001,('clearance gap',max(floor)*scale)
    for value in roots:value[1]+=margin
    root_track['values']=roots
    after,blob=export_gltf(build_model(changed),names)
    (destination/'model.gltf').write_text(json.dumps(after,indent=2)+'\n');(destination/'model.bin').write_bytes(blob)
    (out/'recipes/badger.yaml').write_text(yaml.safe_dump(changed,sort_keys=False),encoding='utf8')
    report={'scope':__doc__,'state':'unapproved_stance_ik_bake','samples':args.samples,'solved':solved,
        'floor_samples':8193,'floor_before_lift_model':[min(floor),max(floor)],'global_micro_lift_model':margin,
        'floor_after_lift_world':[(min(floor)+margin)*scale,(max(floor)+margin)*scale],
        'bin_bytes':len(blob),'builder_sha256':sha(Path(__file__)),'source_sha256':sha(folder/'model.bin'),
        'contract_sha256':sha(out/'support_contract.json'),'bin_sha256':sha(destination/'model.bin')}
    (out/'proposal_report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items()if k!='solved'},indent=2),flush=True)


if __name__=='__main__':main()
