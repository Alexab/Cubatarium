"""Measure exported CreatureGen skinning and render preserved death clips.

Research only: baseline reads never enter the production generator. Numeric
contact evidence and offline asset sheets do not approve appearance or runtime.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
from creaturegen.pipeline import load_recipe
from creaturegen.anatomy import build_model
from review_creature_redesign import accessor, extent, font, gltf_faces, panel, sample_nodes, world_matrices


def measure(folder, model, samples):
    g = json.loads((folder/'model.gltf').read_text(encoding='utf8'))
    blobs = [(folder/b['uri']).read_bytes() for b in g['buffers']]
    definition = json.loads((folder/'creature.json').read_text(encoding='utf8'))
    scale = definition['visual']['gltf'].get('model_scale', 1)
    if len(g['skins']) != 1 or len(g['meshes']) != 1:
        raise ValueError('This diagnostic expects a single CreatureGen skin/mesh')
    skin = g['skins'][0]
    inverse = accessor(g,blobs,skin['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)
    positions=[]; joints=[]; weights=[]
    for primitive in g['meshes'][0]['primitives']:
        a=primitive['attributes']
        positions.append(accessor(g,blobs,a['POSITION']))
        joints.append(accessor(g,blobs,a['JOINTS_0']).astype(int))
        weights.append(accessor(g,blobs,a['WEIGHTS_0']))
    p=np.concatenate(positions);j=np.concatenate(joints);w=np.concatenate(weights)
    vertices=np.column_stack([p,np.ones(len(p))])
    names={g['nodes'][node]['name']:i for i,node in enumerate(skin['joints'])}
    result={}
    for clip,authored in model.motion.get('clips',{}).items():
        contact=authored.get('contact')
        heights={name:[] for name in contact.get('bones',[])} if contact else {}
        full=[]
        for phase in np.linspace(0,1,samples):
            world=world_matrices(sample_nodes(g,blobs,clip,float(phase)))
            palette=np.array([world[node]@inv for node,inv in zip(skin['joints'],inverse)])
            matrices=(palette[j]*w[:,:,None,None]).sum(axis=1)
            posed=np.einsum('nij,nj->ni',matrices,vertices)[:,:3]*scale
            full.append(float(posed[:,1].min()))
            for name in heights:
                # Fully owned effector vertices: never count a joint with no mesh.
                mask=np.any((j==names[name])&(w>.99),axis=1)
                if mask.any():
                    heights[name].append(float(posed[mask,1].min()))
        heights={name:values for name,values in heights.items() if values}
        result[clip]={
            'semantics':'authored_ground_contact' if contact else 'full_mesh_clearance_not_foot_contact',
            'sample_count':samples,
            'min_geometry_y_blocks':round(min(full),6),
            'max_min_geometry_y_blocks':round(max(full),6),
            'owned_effectors':{name:{'min_y_blocks':round(min(v),6),
                'max_min_y_blocks':round(max(v),6)} for name,v in heights.items()},
        }
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--species',required=True)
    parser.add_argument('--qa-models',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--samples',type=int,default=65)
    args=parser.parse_args()
    if args.samples<3:
        parser.error('--samples must be >= 3')
    args.out.mkdir(parents=True,exist_ok=True)
    report={}
    baseline=ROOT/'assetgen_output/creatures/review_2026_11_10/baseline/models/creatures'
    for sid in args.species.split(','):
        print('MEASURE '+sid,flush=True)
        folder=args.qa_models/'creatures'/sid
        recipe=load_recipe(sid);model=build_model(recipe);entry={}
        for label,source in [('baseline',baseline/sid),('candidate',folder)]:
            lo,hi=extent(gltf_faces(source))
            entry[label]={'extent_blocks':(hi-lo).round(6).tolist(),'min_y':round(float(lo[1]),6)}
            if recipe.get('locomotion_axis',recipe.get('forward_axis')) != recipe.get('forward_axis'):
                # Sideways locomotion changes world X/Z footprint, not the
                # animal's anatomy; report both instead of hiding that swap.
                yaw=json.loads((source/'creature.json').read_text(encoding='utf8'))['visual']['gltf'].get('model_yaw_offset_deg',0)
                angle=np.deg2rad(-yaw);co,si=np.cos(angle),np.sin(angle)
                matrix=np.array([[co,0,si],[0,1,0],[-si,0,co]])
                points=np.concatenate([vertices@matrix.T for vertices,_,_ in gltf_faces(source)])
                entry[label]['extent_anatomical_blocks']=np.ptp(points,axis=0).round(6).tolist()
        entry['authored_axes']={k:recipe[k] for k in ('forward_axis','locomotion_axis') if k in recipe}
        entry['candidate_bones']=len(model.bones)
        entry['candidate_clips']=list(model.motion['clips'])
        gltf=json.loads((folder/'model.gltf').read_text(encoding='utf8'))
        blobs=[(folder/b['uri']).read_bytes() for b in gltf['buffers']]
        definition=json.loads((folder/'creature.json').read_text(encoding='utf8'))
        entry['clip_timing']={}
        for animation in gltf['animations']:
            duration=max(float(accessor(gltf,blobs,s['input'])[-1,0]) for s in animation['samplers'])
            speed=definition['visual']['animation']['clips'].get(animation['name'],{}).get('speed',1)
            entry['clip_timing'][animation['name']]={'asset_duration_sec':round(duration,6),
                'runtime_speed':speed,'effective_period_at_unit_travel_scale_sec':round(duration/speed,6)}
        entry['payload_sha256']={name:hashlib.sha256((folder/name).read_bytes()).hexdigest()
                                 for name in ('creature.json','model.gltf','model.bin')}
        entry['contact']=measure(folder,model,args.samples)
        report[sid]=entry
        if 'die' in model.motion['clips']:
            phases=[0,.2,.4,.6,.8,1]
            faces=[gltf_faces(folder,'die',p) for p in phases]
            span=max(float(np.linalg.norm(extent(f)[1]-extent(f)[0])) for f in faces)
            sheet=Image.new('RGB',(960,570),(245,245,242));draw=ImageDraw.Draw(sheet)
            draw.text((8,5),sid+' / die — sampled glTF asset capability, not a runtime Death state',font=font(),fill=(20,25,25))
            for i,(phase,f) in enumerate(zip(phases,faces)):
                x=(i%3)*320;y=35+(i//3)*265
                sheet.paste(panel(f,145,-15,size=240,span=span),(x+40,y+20))
                draw.text((x+8,y),f'phase {phase:.1f}',font=font(),fill=(20,25,25))
            sheet.save(args.out/(sid+'_die_asset.png'))
    (args.out/'metrics.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    print(json.dumps({sid:{clip:{key:stats[key] for key in ('min_geometry_y_blocks','max_min_geometry_y_blocks')}
        for clip,stats in entry['contact'].items()} for sid,entry in report.items()},indent=2))


if __name__=='__main__':main()
