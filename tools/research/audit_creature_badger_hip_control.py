"""Exported hip-controller geometry/motion audit; never an art/stance approval."""
import json
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation
from audit_creature_motion_v5 import inspect, sha
from audit_creature_badger_stride import channels
from build_creature_badger_ankle_control import geometry
from review_creature_redesign import accessor, sample_nodes, world_matrices

ROOT=Path(__file__).resolve().parents[2]


def main():
    source=ROOT/'assetgen_output/creatures/badger_ankle_control_v1/models/creatures/badger'
    folder=ROOT/'assetgen_output/creatures/badger_hip_control_v1/models/creatures/badger'
    a,ab,_=inspect(source); b,bb,mesh=inspect(folder)
    an={n['name']:i for i,n in enumerate(a['nodes'])};bn={n['name']:i for i,n in enumerate(b['nodes'])}
    aw,bw=world_matrices(a['nodes']),world_matrices(b['nodes'])
    old_names=[a['nodes'][i]['name'] for i in a['skins'][0]['joints']]
    assert len(old_names)==17 and len(b['skins'][0]['joints'])==19
    assert all(np.array_equal(aw[an[n]],bw[bn[n]]) for n in old_names)
    ac,bc=channels(a,ab),channels(b,bb)
    assert set(bc)-set(ac)=={('walk','fore_hip_control','rotation'),('walk','rear_hip_control','rotation')}
    for key in ac:
        if key==('walk','body','translation'):continue
        assert ac[key][0]==bc[key][0]
        assert all(np.array_equal(x,y)for x,y in zip(ac[key][1:],bc[key][1:]))
    for ma,mb in zip(a['meshes'],b['meshes']):
        for pa,pb in zip(ma['primitives'],mb['primitives']):
            for name in pa['attributes']:
                assert np.array_equal(accessor(a,ab,pa['attributes'][name]),accessor(b,bb,pb['attributes'][name]))
            assert np.array_equal(accessor(a,ab,pa['indices']),accessor(b,bb,pb['indices']))
    for image in a['images']:
        assert (source/image['uri']).read_bytes()==(folder/image['uri']).read_bytes()
    maximum_angle=0.;maximum_body_angle=0.
    for phase in np.linspace(0,1,1025):
        sa=sample_nodes(a,ab,'walk',float(phase));sb=sample_nodes(b,bb,'walk',float(phase))
        wa,wb=world_matrices(sa),world_matrices(sb)
        for name in ['body','spine','head','jaw','tail']:
            maximum_body_angle=max(maximum_body_angle,float(Rotation.from_matrix(wa[an[name]][:3,:3].T@wb[bn[name]][:3,:3]).magnitude()))
        roll=Rotation.from_matrix(wa[an['body']][:3,:3]).as_euler('xyz')[2]
        target=Rotation.from_euler('z',roll*.4).as_matrix()
        for control,desired in [('fore_hip_control',target),('rear_hip_control',target@Rotation.from_quat(sa[an['spine']]['rotation']).as_matrix())]:
            maximum_angle=max(maximum_angle,float(Rotation.from_matrix(desired.T@wb[bn[control]][:3,:3]).magnitude()))
    # Bound observed interpolation error, not a new artistic acceptance gate.
    assert np.degrees(maximum_angle)<.01 and maximum_body_angle<1e-7
    definition=json.loads((folder/'creature.json').read_text())
    scale=definition['visual']['gltf']['model_scale'];offset=definition['visual']['gltf'].get('model_offset_y',0)
    bind_y=geometry(b,bb,None,0)[:,1].min()
    floors=[float((geometry(b,bb,'walk',float(p))[:,1].min()-bind_y)*scale+offset)for p in np.linspace(0,1,8193)]
    assert min(floors)>=-.001 and max(floors)<=.001
    report={'scope':__doc__,'acceptance':'pending','geometry_bind_uv_weights_png_exact':True,
            'all_original_rotations_exact':True,'old_joint_bind_world_exact':True,'mesh':mesh,
            'max_controller_target_error_degrees':float(np.degrees(maximum_angle)),
            'max_upper_body_rotation_error_degrees':float(np.degrees(maximum_body_angle)),
            'exported_walk_floor_samples':8193,'floor_world_min':min(floors),'floor_world_max':max(floors),
            'analyzer_sha256':sha(Path(__file__)),'gltf_sha256':sha(folder/'model.gltf'),'bin_sha256':sha(folder/'model.bin')}
    out=ROOT/'docs/research/creature-r6-v2/badger-hip-control-v1/invariants.json';out.parent.mkdir(parents=True,exist_ok=True)
    out.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2),flush=True)


if __name__=='__main__':main()
