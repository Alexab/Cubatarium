import sys,json
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import numpy as np
from creaturegen import pipeline
from creaturegen.anatomy import build_model
from creaturegen.export_gltf import export_gltf
from research.review_creature_redesign import accessor,sample_nodes,world_matrices,authored_vertex_owners
out=[]
for sid in ('orc','tree_monster','treeman','golem'):
 m=build_model(pipeline.load_recipe(sid));g,b=export_gltf(m,list(m.motion['clips']))
 definition=pipeline.runtime_definition(sid,Path(__file__).resolve().parents[2]/'assetgen_output/creatures/player_stance_probe')
 spec=definition['visual']['gltf'];yaw=np.deg2rad(spec.get('model_yaw_offset_deg',0));scale=spec.get('model_scale',1)
 rotation=np.array([[np.cos(yaw),0,np.sin(yaw)],[0,1,0],[-np.sin(yaw),0,np.cos(yaw)]])
 travel_speed=definition['locomotion']['walk_speed']
 a=g['meshes'][0]['primitives'][0]['attributes'];p=accessor(g,[b],a['POSITION']);v=np.column_stack([p,np.ones(len(p))])
 j=accessor(g,[b],a['JOINTS_0']).astype(int);w=accessor(g,[b],a['WEIGHTS_0']);inv=accessor(g,[b],g['skins'][0]['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)
 names={x.name:i for i,x in enumerate(m.bones)};owners=authored_vertex_owners(m,g,[b])
 for clip in ('walk','run'):
  cfg=m.motion['clips'][clip];duration=max(t['times'][-1] for t in cfg['tracks'])/cfg['runtime_speed']
  for side in ('left','right'):
   mask=(owners==names['foot_'+side])&(w[:,0]>.99);poses=[]
   for t in np.linspace(0,1,513):
    world=world_matrices(sample_nodes(g,[b],clip,float(t)));pal=np.array([world[n]@iv for n,iv in zip(g['skins'][0]['joints'],inv)])
    q=np.einsum('nij,nj->ni',(pal[j[mask]]*w[mask,:,None,None]).sum(axis=1),v[mask])[:,:3]
    q=(q*scale)@rotation.T+np.array([0,spec.get('model_offset_y',0),travel_speed*duration*t]);poses.append(q)
   h=np.array([q[:,1].min() for q in poses]);z=np.array([q[:,2].mean() for q in poses]);speed=np.diff(z)/(duration/512)
   stance=np.maximum(h[:-1],h[1:])<.001
   out.append(dict(species=sid,clip=clip,side=side,coordinates='flat-world, body moves along +Z; centroid velocity is not an exact sole-contact witness',body_speed=travel_speed,min_height=float(min(h)),max_height=float(max(h)),stance_intervals=int(sum(stance)),stance_world_centroid_forward_speed_min=float(min(speed[stance])) if stance.any() else None,stance_world_centroid_forward_speed_max=float(max(speed[stance])) if stance.any() else None,stance_world_centroid_forward_speed_median=float(np.median(speed[stance])) if stance.any() else None))
print(json.dumps(out,indent=2))
