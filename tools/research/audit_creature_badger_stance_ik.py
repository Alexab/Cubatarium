"""Independent exported kinematic/contact/seam audit; offline, no art approval."""
import json
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation
import yaml
from audit_creature_motion_v5 import inspect, sha
from review_creature_redesign import sample_nodes, world_matrices, accessor
from build_creature_badger_ankle_control import geometry
from audit_creature_badger_stride import channels

ROOT=Path(__file__).resolve().parents[2]


def box(cube, world, pivot):
    centre=(np.array(cube['origin'])+np.array(cube['size'])/2)/16-np.array(pivot)/16
    return (world@np.r_[centre,1])[:3],world[:3,:3],np.array(cube['size'])/32


def separation(a,b):
    ca,ra,ha=a;cb,rb,hb=b
    axes=[*ra.T,*rb.T]+[np.cross(x,y)for x in ra.T for y in rb.T]
    gaps=[]
    for axis in axes:
        n=np.linalg.norm(axis)
        if n<1e-8:continue
        axis=axis/n
        gaps.append(abs((cb-ca)@axis)-np.abs(axis@ra)@ha-np.abs(axis@rb)@hb)
    return max(gaps)


def main():
    proposal=ROOT/'assetgen_output/creatures/badger_stance_ik_v2'
    folder=proposal/'models/creatures/badger'
    source=ROOT/'assetgen_output/creatures/badger_hip_control_v1/models/creatures/badger'
    out=ROOT/'docs/research/creature-r6-v2/badger-stance-ik-v2';out.mkdir(parents=True,exist_ok=True)
    g,blobs,mesh=inspect(folder);sg,sb,_=inspect(source)
    contract=json.loads((proposal/'support_contract.json').read_text());report=json.loads((proposal/'proposal_report.json').read_text())
    assert sha(proposal/'support_contract.json')==report['contract_sha256'] and sha(folder/'model.bin')==report['bin_sha256']
    recipe=yaml.safe_load((proposal/'recipes/badger.yaml').read_text(encoding='utf8'))
    bones={b['name']:b for b in recipe['anatomy']['bones']}
    nodes={n['name']:i for i,n in enumerate(g['nodes'])};bind=world_matrices(g['nodes'])
    definition=json.loads((folder/'creature.json').read_text());scale=definition['visual']['gltf']['model_scale']
    animation=next(a for a in g['animations']if a['name']=='walk')
    duration=max(float(accessor(g,blobs,s['input'])[-1,0])for s in animation['samplers'])
    # Structural/payload comparison: joints/hierarchy/mesh untouched except
    # deliberately reassigning named lower paw geometry to its foot joint.
    assert sg['nodes']==g['nodes'] and sg['skins']==g['skins']
    for ma,mb in zip(sg['meshes'],g['meshes']):
        for pa,pb in zip(ma['primitives'],mb['primitives']):
            for name in ['POSITION','TEXCOORD_0']:
                assert np.array_equal(accessor(sg,sb,pa['attributes'][name]),accessor(g,blobs,pb['attributes'][name]))
            assert np.array_equal(accessor(sg,sb,pa['indices']),accessor(g,blobs,pb['indices']))
            sj=accessor(sg,sb,pa['attributes']['JOINTS_0']).astype(int);sw=accessor(sg,sb,pa['attributes']['WEIGHTS_0'])
            nj=accessor(g,blobs,pb['attributes']['JOINTS_0']).astype(int);nw=accessor(g,blobs,pb['attributes']['WEIGHTS_0'])
            skin_names=[sg['nodes'][i]['name']for i in sg['skins'][0]['joints']]
            changed_mask=np.zeros(len(sj),dtype=bool)
            for suffix in ['fl','fr','bl','br']:
                foot_joint=skin_names.index('foot_'+suffix);lower_joint=skin_names.index('lower_'+suffix)
                mask=np.sum(np.where(np.isin(sj,[foot_joint,lower_joint]),sw,0),axis=1)>.9999
                assert np.all(np.sum(np.where(nj[mask]==foot_joint,nw[mask],0),axis=1)==1)
                changed_mask|=mask
            assert np.array_equal(sj[~changed_mask],nj[~changed_mask]) and np.array_equal(sw[~changed_mask],nw[~changed_mask])
    for image in g['images']:assert (source/image['uri']).read_bytes()==(folder/image['uri']).read_bytes()
    old,new=channels(sg,sb),channels(g,blobs)
    for key in old:
        if key[0]=='walk'and(key[1].startswith(('upper_','lower_'))or key==('walk','body','translation')):continue
        assert old[key][0]==new[key][0] and all(np.array_equal(a,b)for a,b in zip(old[key][1:],new[key][1:]))
    assert set(new)-set(old)=={('walk','foot_'+s,'rotation')for s in ['fl','fr','bl','br']}
    errors=[];orientations=[];heights=[];gaps=[];floors=[];length_errors=[];episodes={};knees=[];foot_angles=[];worst_gap=None
    bind_y=geometry(g,blobs,None,0)[:,1].min()
    for phase in np.linspace(0,1,2049):
        sampled=sample_nodes(g,blobs,'walk',float(phase));world=world_matrices(sampled)
        floors.append(float((geometry(g,blobs,'walk',float(phase))[:,1].min()-bind_y)*scale))
        for suffix in ['fl','fr','bl','br']:
            upper,lower,foot=[nodes[p+'_'+suffix]for p in ['upper','lower','foot']]
            origin=bind[foot][:3,3];local=np.array([0.,-origin[1],.5/16,1.])
            actual=(world[foot]@local)[:3]
            p=(phase*contract['gait_cycles']+contract['phase_offsets'][suffix])%1.
            if p<contract['stance_duty']:
                target=origin.copy();target[2]+=.5/16+contract['stride_model']*(.25-p)
                errors.append(float(np.linalg.norm((actual-target)[[0,2]])*scale))
                heights.append(float(actual[1]*scale))
                orientations.append(float(np.degrees(Rotation.from_matrix(world[foot][:3,:3]).magnitude())))
                cycle=int(np.floor(phase*contract['gait_cycles']+contract['phase_offsets'][suffix]))
                position=actual[[0,2]]+np.array([0.,contract['travel_model_per_clip_second']*phase*duration])
                episodes.setdefault((suffix,cycle),[]).append(position*scale)
            for child,parent in [(lower,upper),(foot,lower)]:
                length_errors.append(abs(np.linalg.norm(world[child][:3,3]-world[parent][:3,3])-np.linalg.norm(g['nodes'][child]['translation'])))
            knees.append({'stance':bool(p<.5),'degrees':float(np.degrees(Rotation.from_quat(sampled[lower]['rotation']).magnitude()))})
            foot_angles.append(float(np.degrees(Rotation.from_quat(sampled[foot]['rotation']).magnitude())))
            upper_cube=next(c for c in bones['upper_'+suffix]['cubes']if c['name']=='upper_'+suffix)
            paw_cube=next(c for c in bones['lower_'+suffix]['cubes']if c['name']=='foot_'+suffix)
            gap=separation(box(upper_cube,world[upper],bones['upper_'+suffix]['pivot']),box(paw_cube,world[foot],bones['foot_'+suffix]['pivot']))*scale
            gaps.append(float(gap))
            if worst_gap is None or gap>worst_gap['gap_world']:worst_gap={'phase':float(phase),'foot':suffix,'gap_world':float(gap)}
    drift=[float(np.linalg.norm(v[-1]-v[0]))for v in episodes.values()if len(v)>3]
    result={'scope':__doc__,'art_acceptance':'pending','runtime_world_acceptance':'pending',
        'geometry_uv_png_exact':True,'unchanged_upper_body_and_other_clip_channels':True,
        'foot_and_claws_single_rigid_transform':True,'non_foot_weights_exact':True,
        'samples':2049,'max_support_xz_error_world':max(errors),'support_y_world_range':[min(heights),max(heights)],
        'max_support_orientation_error_degrees':max(orientations),'nominal_offline_max_plant_endpoint_drift_world':max(drift),
        'bone_length_max_error_model':max(length_errors),'max_foot_local_rotation_degrees':max(foot_angles),
        'knee_stance_max_degrees':max(k['degrees']for k in knees if k['stance']),
        'knee_swing_max_degrees':max(k['degrees']for k in knees if not k['stance']),
        'upper_paw_disconnected_samples':sum(g>1e-5 for g in gaps),'worst_upper_paw_separation':worst_gap,
        'floor_world_range':[min(floors),max(floors)],'mesh':mesh,'analyzer_sha256':sha(Path(__file__)),
        'proposal_bin_sha256':sha(folder/'model.bin'),'contract_sha256':sha(proposal/'support_contract.json')}
    result['kinematic_checks_pass']=max(errors)<.001 and max(length_errors)<1e-7 and min(floors)>=-.001 and max(floors)<=.001
    result['attachment_checks_pass']=result['upper_paw_disconnected_samples']==0
    (out/'kinematics.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2),flush=True)


if __name__=='__main__':main()
