"""Tests for authored anatomy, deformation, clip preservation and source ownership."""
from __future__ import annotations

import copy
from functools import lru_cache
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

import yaml
import numpy as np

from creaturegen import pipeline
from creaturegen.anatomy import build_model
from creaturegen.export_gltf import export_gltf
from creaturegen.export_rigid import export_rigid
from creaturegen.quality import bundle_hash
from creaturegen.surface_materials import surface_point
from research.audit_creature_coplanar_faces import overlaps
from research.review_creature_redesign import accessor, sample_nodes, world_matrices, authored_vertex_owners


class FidelityTests(unittest.TestCase):
    def test_gltf_uv_faces_match_anatomical_surface_coordinates(self):
        # A non-square box exposes swapped side islands and mirrored Z masks.
        recipe=copy.deepcopy(pipeline.load_recipe('shark'))
        recipe['anatomy']={'bones':[{'name':'body','pivot':[0,0,0],'cubes':[
            {'name':'probe','origin':[-2,3,-7],'size':[4,5,9],'role':'body'}]}]}
        recipe['motion']={}
        model=build_model(recipe);cube=model.cubes()[0]
        g,blob=export_gltf(model,[]);a=g['meshes'][0]['primitives'][0]['attributes']
        positions=accessor(g,[blob],a['POSITION'])*16
        uv=accessor(g,[blob],a['TEXCOORD_0'])*np.array(model.texture_size)
        u,v=cube.uv;w,h,d=cube.size
        regions={'west':(u,v+d,d,h),'north':(u+d,v+d,w,h),
                 'east':(u+d+w,v+d,d,h),'south':(u+2*d+w,v+d,w,h),
                 'up':(u+d,v,w,d),'down':(u+d+w,v,w,d)}
        for face_index,face in enumerate(('south','north','west','east','up','down')):
            x0,y0,width,height=regions[face]
            for i in range(4):
                index=face_index*4+i
                # Undo the intentional half-texel border inset.
                a=(uv[index,0]-x0-.5)/(width-1)
                b=(uv[index,1]-y0-.5)/(height-1)
                self.assertTrue(np.allclose(surface_point(cube,face,a,b),positions[index],atol=1e-4),face)

    def test_authored_gltf_head_faces_entity_forward(self):
        for sid in ('badger','stingray','butterfly','hermitcrab','rat','kitten','hedgehog','panda','warthog','penguin','owl','puffin','wasp','shark','seahorse','manatee','seal','whale','water_dragon','lobster','octopus'):
            recipe=pipeline.load_recipe(sid);model=build_model(recipe)
            g,blob=export_gltf(model,list(model.motion['clips']))
            with tempfile.TemporaryDirectory() as temp:
                yaw=pipeline.runtime_definition(sid,Path(temp))['visual']['gltf']['model_yaw_offset_deg']
            a=g['meshes'][0]['primitives'][0]['attributes']
            p=accessor(g,[blob],a['POSITION']);j=accessor(g,[blob],a['JOINTS_0']).astype(int)
            head=next(i for i,b in enumerate(model.bones) if b.name=='head')
            body=next(b for b in model.bones if b.name=='body')
            delta=p[authored_vertex_owners(model,g,[blob])==head].mean(axis=0)[2]-body.pivot[2]/16
            self.assertGreater(delta*np.cos(np.deg2rad(yaw)),0,sid+': backwards-facing head')
        invalid=copy.deepcopy(pipeline.load_recipe('owl'));del invalid['forward_axis']
        with patch.object(pipeline,'load_recipe',return_value=invalid):
            with tempfile.TemporaryDirectory() as temp, self.assertRaises(ValueError):
                pipeline.runtime_definition('owl',Path(temp))

    def test_warriors_preserve_attached_limbs_stride_and_weapon_action(self):
        for sid,count in [('oerkki',21),('orc',18),('ogre',18),('dungeon_master',17),('land_guard',17)]:
            model=build_model(pipeline.load_recipe(sid));g,blob=export_gltf(model,list(model.motion['clips']))
            self.assertEqual({a['name'] for a in g['animations']},{'idle','walk','run','punch'})
            a=g['meshes'][0]['primitives'][0]['attributes']
            p=accessor(g,[blob],a['POSITION']);v=np.column_stack([p,np.ones(len(p))])
            j=accessor(g,[blob],a['JOINTS_0']).astype(int);w=accessor(g,[blob],a['WEIGHTS_0'])
            inv=accessor(g,[blob],g['skins'][0]['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)
            names={b.name:i for i,b in enumerate(model.bones)}
            self.assertEqual(len(set(j[w>.001])),count)
            @lru_cache(maxsize=None)
            def sample(clip,phase):
                world=world_matrices(sample_nodes(g,[blob],clip,phase))
                palette=np.array([world[n]@iv for n,iv in zip(g['skins'][0]['joints'],inv)])
                return np.einsum('nij,nj->ni',(palette[j]*w[:,:,None,None]).sum(axis=1),v)[:,:3],palette
            for clip in ('idle','walk','run'):
                self.assertTrue(np.allclose(sample(clip,0)[0],sample(clip,1)[0],atol=1e-5),sid+': loop seam')
            for clip in ('idle','walk','run','punch'):
                # Include half-key phases: authored contact keys alone cannot
                # prove that quaternion interpolation keeps the sole clear.
                for i in range(257):
                    q,_=sample(clip,i/256)
                    self.assertGreaterEqual(q[:,1].min(),-.001,sid+': mesh or weapon penetrates floor')
                    self.assertLess(q[:,1].min(),.001,sid+': floating contact')
                for side in ('left','right'):
                    for proximal,distal in [('thigh_'+side,'shin_'+side),('upper_arm_'+side,'forearm_'+side)]:
                        left=np.flatnonzero(authored_vertex_owners(model,g,[blob])==names[proximal]);right=np.flatnonzero(authored_vertex_owners(model,g,[blob])==names[distal])
                        pairs=[(u,int(right[np.argmin(np.linalg.norm(p[right]-p[u],axis=1))])) for u in left
                               if np.linalg.norm(p[right]-p[u],axis=1).min()<1e-6]
                        self.assertTrue(pairs,sid+': '+proximal+' has no matching distal ring')
                        bone=model.bones[names[proximal]]
                        roots=(authored_vertex_owners(model,g,[blob])==names[proximal])&np.isclose(p[:,1],bone.pivot[1]/16)
                        self.assertTrue(roots.any(),sid+': missing proximal ring')
                        for phase in (.125,.375,.625):
                            q,palette=sample(clip,phase)
                            self.assertLess(max(np.linalg.norm(q[u]-q[v]) for u,v in pairs),1e-5,sid+': open joint')
                            expected=(v[roots]@palette[names[bone.parent]].T)[:,:3]
                            self.assertTrue(np.allclose(q[roots],expected,atol=1e-5),sid+': proximal attachment')
                    if clip in ('walk','run'):
                        mask=(authored_vertex_owners(model,g,[blob])==names['foot_'+side])&(w[:,0]>.99)
                        poses=[sample(clip,i/64)[0][mask] for i in range(65)]
                        heights=[q[:,1].min() for q in poses]
                        self.assertGreater(max(heights),.03,sid+': no swing')
                        retreat=[-(poses[i+1][:,2]-poses[i][:,2]).mean() for i in range(64) if max(heights[i:i+2])<.001]
                        self.assertTrue(retreat);self.assertLess(sum(retreat),-.05,sid+': wrong stance')
                        if side=='left':
                            definition=pipeline.runtime_definition(sid,Path(tempfile.gettempdir()));settings=model.motion['clips'][clip]
                            duration=max(t['times'][-1] for t in settings['tracks'])
                            speed=(poses[32][:,2]-poses[0][:,2]).mean()/(duration*.5/settings['runtime_speed'])
                            self.assertAlmostEqual(speed,definition['locomotion']['walk_speed'],places=4)
            if sid!='oerkki':
                uv=accessor(g,[blob],a['TEXCOORD_0'])*np.array(model.texture_size)
                for cube in model.cubes():
                    if not cube.name.startswith(('shoulder_','cuff_')):continue
                    u,t=cube.uv;cw,ch,cd=cube.size
                    mask=(uv[:,0]>u)&(uv[:,0]<u+2*(cw+cd))&(uv[:,1]>t)&(uv[:,1]<t+ch+cd)
                    self.assertEqual(mask.sum(),24,'unique pauldron atlas island')
                    rest=np.linalg.norm(p[mask,None,:]-p[None,mask,:],axis=2)
                    for phase in (.125,.5,.75):
                        q=sample('punch',phase)[0][mask]
                        posed=np.linalg.norm(q[:,None,:]-q[None,:,:],axis=2)
                        self.assertTrue(np.allclose(rest,posed,atol=1e-5),'rigid armor bends like flesh')
            if sid=='ogre':
                self.assertEqual(model.bones[names['club']].parent,'hand_left')
                club=(authored_vertex_owners(model,g,[blob])==names['club'])&(w[:,0]>.99)&(p[:,2]<-29/16)
                high=sample('punch',.3)[0][club,1].mean();low=sample('punch',.62)[0][club,1].mean()
                self.assertGreater(high-low,1,'club must lift and strike')
            elif sid=='oerkki':
                for side in ('left','right'):
                    for finger in range(2):
                        mask=(authored_vertex_owners(model,g,[blob])==names[f'claw_{side}{finger}'])&(w[:,0]>.99)
                        self.assertTrue(mask.any())
                        # Curl relative to its hand, rather than counting
                        # whole-arm displacement as articulated claws.
                        q0,p0=sample('punch',0);q1,p1=sample('punch',.5)
                        hand=names['hand_'+side]
                        local0=np.column_stack([q0[mask],np.ones(mask.sum())])@np.linalg.inv(p0[hand]).T
                        local1=np.column_stack([q1[mask],np.ones(mask.sum())])@np.linalg.inv(p1[hand]).T
                        self.assertGreater(np.linalg.norm(local1-local0,axis=1).max(),.015)

    def test_arboreal_stone_contact_and_functional_rigs(self):
        for sid,count in [('tree_monster',27),('treeman',23),('golem',16)]:
            model=build_model(pipeline.load_recipe(sid));g,blob=export_gltf(model,list(model.motion['clips']))
            self.assertEqual({a['name'] for a in g['animations']},{'idle','walk','run','punch'})
            a=g['meshes'][0]['primitives'][0]['attributes']
            p=accessor(g,[blob],a['POSITION']);v=np.column_stack([p,np.ones(len(p))])
            j=accessor(g,[blob],a['JOINTS_0']).astype(int);w=accessor(g,[blob],a['WEIGHTS_0'])
            inv=accessor(g,[blob],g['skins'][0]['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)
            names={b.name:i for i,b in enumerate(model.bones)}
            self.assertEqual(len(set(j[w>.001])),count,sid)
            @lru_cache(maxsize=None)
            def sample(clip,phase):
                world=world_matrices(sample_nodes(g,[blob],clip,phase))
                palette=np.array([world[n]@iv for n,iv in zip(g['skins'][0]['joints'],inv)])
                return np.einsum('nij,nj->ni',(palette[j]*w[:,:,None,None]).sum(axis=1),v)[:,:3],palette
            for clip in ('idle','walk','run'):
                self.assertTrue(np.allclose(sample(clip,0)[0],sample(clip,1)[0],atol=1e-5),sid+': loop seam')
            for clip in ('idle','walk','run','punch'):
                for i in range(513):
                    q,_=sample(clip,i/512)
                    self.assertGreaterEqual(q[:,1].min(),-.001,sid+': floor penetration')
                    self.assertLess(q[:,1].min(),.001,sid+': floating')
            for side in ('left','right'):
                mask=(authored_vertex_owners(model,g,[blob])==names['foot_'+side])&(w[:,0]>.99)
                for clip in ('walk','run'):
                    poses=[sample(clip,i/64)[0][mask] for i in range(65)]
                    heights=[q[:,1].min() for q in poses]
                    self.assertGreater(max(heights),.03,sid+': no swing')
                    retreat=[-(poses[i+1][:,2]-poses[i][:,2]).mean() for i in range(64) if max(heights[i:i+2])<.001]
                    self.assertTrue(retreat);self.assertLess(sum(retreat),-.05,sid+': backwards stance')
                    settings=model.motion['clips'][clip];duration=max(t['times'][-1] for t in settings['tracks'])
                    # Right leg starts half a cycle later; measure its stance half.
                    start,end=(0,32) if side=='left' else (32,64)
                    speed=(poses[end][:,2]-poses[start][:,2]).mean()/(duration*.5/settings['runtime_speed'])
                    definition=pipeline.runtime_definition(sid,Path(tempfile.gettempdir()))
                    self.assertAlmostEqual(speed,definition['locomotion']['walk_speed'],places=4)
            if sid=='golem':
                uv=accessor(g,[blob],a['TEXCOORD_0'])*np.array(model.texture_size)
                for cube in model.cubes():
                    if cube.name=='waist_connector':continue
                    u,t=cube.uv;cw,ch,cd=cube.size
                    mask=(uv[:,0]>u)&(uv[:,0]<u+2*(cw+cd))&(uv[:,1]>t)&(uv[:,1]<t+ch+cd)
                    self.assertEqual(mask.sum(),24)
                    rest=np.linalg.norm(p[mask,None,:]-p[None,mask,:],axis=2)
                    for clip,phase in [('walk',.25),('run',.625),('punch',.5)]:
                        q=sample(clip,phase)[0][mask]
                        self.assertTrue(np.allclose(rest,np.linalg.norm(q[:,None,:]-q[None,:,:],axis=2),atol=1e-5),'stone bends')
                head=p[authored_vertex_owners(model,g,[blob])==names['head']];trunk=p[authored_vertex_owners(model,g,[blob])==names['spine']]
                self.assertLess(head[:,1].max(),trunk[:,1].max(),'golem must remain hunched')
                hand=(authored_vertex_owners(model,g,[blob])==names['hand_left'])&(w[:,0]>.99)
                self.assertGreater(sample('punch',.5)[0][hand,1].mean()-sample('punch',0)[0][hand,1].mean(),.5)
            else:
                chains=[('upper_arm_'+s,'forearm_'+s) for s in ('left','right')]
                if sid=='treeman':chains += [('thigh_'+s,'shin_'+s) for s in ('left','right')]
                else:chains += [('bough'+str(i),'bough_tip'+str(i)) for i in range(6)]
                for proximal,distal in chains:
                    left=np.flatnonzero(authored_vertex_owners(model,g,[blob])==names[proximal]);right=np.flatnonzero(authored_vertex_owners(model,g,[blob])==names[distal])
                    pairs=[(u,int(right[np.argmin(np.linalg.norm(p[right]-p[u],axis=1))])) for u in left if np.linalg.norm(p[right]-p[u],axis=1).min()<1e-6]
                    self.assertTrue(pairs,sid+': missing shared branch section')
                    for clip in ('walk','run','punch'):
                        for phase in (.125,.375,.625):
                            q,_=sample(clip,phase)
                            self.assertLess(max(np.linalg.norm(q[u]-q[v]) for u,v in pairs),1e-5,sid+': open wood joint')
                if sid=='treeman':
                    for side in ('left','right'):
                        for finger in range(4):
                            mask=(authored_vertex_owners(model,g,[blob])==names[f'finger_{side}{finger}'])&(w[:,0]>.99)
                            self.assertTrue(mask.any())
                            q0,p0=sample('punch',0);q1,p1=sample('punch',.5);hand=names['hand_'+side]
                            local0=np.column_stack([q0[mask],np.ones(mask.sum())])@np.linalg.inv(p0[hand]).T
                            local1=np.column_stack([q1[mask],np.ones(mask.sum())])@np.linalg.inv(p1[hand]).T
                            self.assertGreater(np.linalg.norm(local1-local0,axis=1).max(),.015,'twig finger does not curl')

    def test_crystal_and_lava_preserve_functional_motion_and_clearance(self):
        for sid,count in [('mese_monster',15),('lava_flan',6)]:
            model=build_model(pipeline.load_recipe(sid));g,blob=export_gltf(model,list(model.motion['clips']))
            expected={'idle','walk','run','punch'}|({'die'} if sid=='mese_monster' else set())
            self.assertEqual({a['name'] for a in g['animations']},expected)
            a=g['meshes'][0]['primitives'][0]['attributes'];p=accessor(g,[blob],a['POSITION']);v=np.column_stack([p,np.ones(len(p))])
            j=accessor(g,[blob],a['JOINTS_0']).astype(int);w=accessor(g,[blob],a['WEIGHTS_0'])
            inv=accessor(g,[blob],g['skins'][0]['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)
            names={b.name:i for i,b in enumerate(model.bones)};scale=float(pipeline.load_recipe(sid).get('model_scale',1))
            self.assertEqual(len(set(j[w>.001])),count)
            @lru_cache(maxsize=None)
            def sample(clip,phase):
                world=world_matrices(sample_nodes(g,[blob],clip,phase));palette=np.array([world[n]@iv for n,iv in zip(g['skins'][0]['joints'],inv)])
                return np.einsum('nij,nj->ni',(palette[j]*w[:,:,None,None]).sum(axis=1),v)[:,:3]*scale,palette
            for clip in ('idle','walk','run'):
                self.assertTrue(np.allclose(sample(clip,0)[0],sample(clip,1)[0],atol=1e-5),sid+': loop seam')
            for clip in expected:
                for i in range(513):
                    low=sample(clip,i/512)[0][:,1].min()
                    self.assertGreaterEqual(low,-.001,sid+': mesh below floor')
                    if sid=='lava_flan':self.assertLess(abs(low),.001,'flow base must stay on its origin plane')
                    elif clip!='die':self.assertGreater(low,.6,'original hovering posture must survive')
            if sid=='mese_monster':
                self.assertLess(abs(sample('die',1)[0][:,1].min()),.001,'death must settle')
                self.assertGreater(np.linalg.norm(sample('die',1)[0]-sample('die',0)[0],axis=1).max(),1)
                chains=[('tail1','tail2'),('tail2','tail3')]+[(f'wing_{side}{i}',f'wing_{side}{i+1}') for side in ('left','right') for i in range(2)]
                for proximal,distal in chains:
                    left=np.flatnonzero(authored_vertex_owners(model,g,[blob])==names[proximal]);right=np.flatnonzero(authored_vertex_owners(model,g,[blob])==names[distal])
                    pairs=[(u,int(right[np.argmin(np.linalg.norm(p[right]-p[u],axis=1))])) for u in left if np.linalg.norm(p[right]-p[u],axis=1).min()<1e-6]
                    self.assertTrue(pairs,'missing common tail/wing cross section')
                    for clip in expected:
                        for phase in (.125,.375,.625):
                            q,_=sample(clip,phase)
                            self.assertLess(max(np.linalg.norm(q[u]-q[v]) for u,v in pairs),1e-5,'crystal chain opens')
                for name in ['jaw','pinch_upper','pinch_lower','wing_left2','wing_right2']:
                    mask=(authored_vertex_owners(model,g,[blob])==names[name])&(w[:,0]>.99);self.assertTrue(mask.any())
                    parent=names[model.bones[names[name]].parent]
                    q0,p0=sample('punch' if not name.startswith('wing') else 'walk',0)
                    q1,p1=sample('punch' if not name.startswith('wing') else 'walk',.5 if not name.startswith('wing') else .25)
                    # Work in the parent's posed frame: body motion alone is insufficient.
                    local0=np.column_stack([q0[mask],np.ones(mask.sum())])@np.linalg.inv(p0[parent]).T
                    local1=np.column_stack([q1[mask],np.ones(mask.sum())])@np.linalg.inv(p1[parent]).T
                    self.assertGreater(np.linalg.norm(local1-local0,axis=1).max(),.01,name)
            else:
                for side in ['head','jaw']:
                    mask=(authored_vertex_owners(model,g,[blob])==names[side])&(w[:,0]>.99)&(p[:,2]<-4.4/16)
                    self.assertTrue(mask.any(),'missing mouth front vertices')
                    rest=sample('punch',0)[0][mask,1].mean();opened=sample('punch',.5)[0][mask,1].mean()
                    self.assertGreater((opened-rest)*(1 if side=='head' else -1),.02,'mouth must visibly open before closure')
                self.assertGreater(np.ptp([sample('run',i/8)[0][:,1].max() for i in range(9)]),.1,'lava must squash and stretch')

    def test_guardians_keep_low_heads_and_distinct_attack_phases(self):
        for sid in ('dungeon_master','land_guard'):
            model=build_model(pipeline.load_recipe(sid));g,blob=export_gltf(model,list(model.motion['clips']))
            a=g['meshes'][0]['primitives'][0]['attributes']
            p=accessor(g,[blob],a['POSITION']);v=np.column_stack([p,np.ones(len(p))])
            j=accessor(g,[blob],a['JOINTS_0']).astype(int);w=accessor(g,[blob],a['WEIGHTS_0'])
            inv=accessor(g,[blob],g['skins'][0]['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)
            names={b.name:i for i,b in enumerate(model.bones)}
            def sample(phase):
                world=world_matrices(sample_nodes(g,[blob],'punch',phase))
                palette=np.array([world[n]@iv for n,iv in zip(g['skins'][0]['joints'],inv)])
                return np.einsum('nij,nj->ni',(palette[j]*w[:,:,None,None]).sum(axis=1),v)[:,:3]
            head=p[authored_vertex_owners(model,g,[blob])==names['head']];trunk=p[authored_vertex_owners(model,g,[blob])==names['spine']]
            self.assertLess(head[:,1].max()-trunk[:,1].max(),.2,'head must stay low among shoulders')
            self.assertLess(head[:,2].min(),trunk[:,2].min()-.2,'head must project in front of the chest')
            self.assertGreater(np.ptp(trunk[:,0]),np.ptp(head[:,0])*1.7,'broad upper torso must dominate')
            with tempfile.TemporaryDirectory() as temp:
                definition=pipeline.runtime_definition(sid,Path(temp))
            yaw=np.deg2rad(definition['visual']['gltf']['model_yaw_offset_deg'])
            centre=head.mean(axis=0)
            self.assertGreater(-np.sin(yaw)*centre[0]+np.cos(yaw)*centre[2],.2,'head must face entity forward')
            self.assertTrue(np.allclose(sample(0),sample(1),atol=1e-5),'attack must recover its resting pose')
            for side in ('left','right'):
                hand=(authored_vertex_owners(model,g,[blob])==names['hand_'+side])&(w[:,0]>.99)
                rest=sample(0)[hand].mean(axis=0)
                if sid=='dungeon_master':
                    before=sample(.3)[hand].mean(axis=0);strike=sample(.65)[hand].mean(axis=0)
                    self.assertGreater(before[2],rest[2]+.1,'no visible attack anticipation')
                    self.assertLess(strike[2],rest[2]-.8,'strike must reach forwards')
                else:
                    guard=sample(.35)[hand].mean(axis=0);strike=sample(.7)[hand].mean(axis=0)
                    self.assertGreater(guard[1],rest[1]+.6,'forearms must lift to guard')
                    self.assertLess(strike[2],rest[2]-.8,'guard must release into a forward strike')

    def test_elemental_bipeds_keep_closed_joints_and_forward_stance(self):
        for sid in ('sand_monster','stone_monster','dirt_monster'):
            model=build_model(pipeline.load_recipe(sid));g,blob=export_gltf(model,list(model.motion['clips']))
            # Horizontal caps of the split limbs lie inside the chest at
            # rest; only exposed side planes can flicker in this rig.
            self.assertEqual([o for o in overlaps(model) if o[2]!='y'],[],sid)
            self.assertEqual({a['name'] for a in g['animations']},{'idle','walk','run','punch'})
            a=g['meshes'][0]['primitives'][0]['attributes']
            p=accessor(g,[blob],a['POSITION']);v=np.column_stack([p,np.ones(len(p))])
            j=accessor(g,[blob],a['JOINTS_0']).astype(int);w=accessor(g,[blob],a['WEIGHTS_0'])
            inv=accessor(g,[blob],g['skins'][0]['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)
            names={b.name:i for i,b in enumerate(model.bones)}
            self.assertEqual(len(set(j[w>.001])),17)
            @lru_cache(maxsize=None)
            def sample(clip,phase):
                world=world_matrices(sample_nodes(g,[blob],clip,phase))
                palette=np.array([world[n]@iv for n,iv in zip(g['skins'][0]['joints'],inv)])
                return np.einsum('nij,nj->ni',(palette[j]*w[:,:,None,None]).sum(axis=1),v)[:,:3],palette
            for clip in ('idle','walk','run'):
                self.assertTrue(np.allclose(sample(clip,0)[0],sample(clip,1)[0],atol=1e-5),sid)
            for clip in ('walk','run','punch'):
                for phase in range(65):
                    q,_=sample(clip,phase/64)
                    self.assertGreaterEqual(q[:,1].min(),-.001)
                    self.assertLess(q[:,1].min(),.001)
                for side in ('left','right'):
                    for proximal,distal in [('thigh_'+side,'shin_'+side),('upper_arm_'+side,'forearm_'+side)]:
                        left=np.flatnonzero(authored_vertex_owners(model,g,[blob])==names[proximal]);right=np.flatnonzero(authored_vertex_owners(model,g,[blob])==names[distal])
                        pairs=[(u,int(right[np.argmin(np.linalg.norm(p[right]-p[u],axis=1))])) for u in left
                               if np.linalg.norm(p[right]-p[u],axis=1).min()<1e-6]
                        self.assertTrue(pairs,'missing joint cross-section')
                        bone=model.bones[names[proximal]];axis=bone.pivot[1]/16
                        roots=(authored_vertex_owners(model,g,[blob])==names[proximal])&np.isclose(p[:,1],axis)
                        self.assertTrue(roots.any())
                        for phase in (.125,.375,.625):
                            q,palette=sample(clip,phase)
                            self.assertLess(max(np.linalg.norm(q[u]-q[v]) for u,v in pairs),1e-5,'joint opens')
                            expected=(v[roots]@palette[names[bone.parent]].T)[:,:3]
                            self.assertTrue(np.allclose(q[roots],expected,atol=1e-5),'proximal ring detaches')
                    mask=(authored_vertex_owners(model,g,[blob])==names['foot_'+side])&(w[:,0]>.99)
                    poses=[sample(clip,i/64)[0][mask] for i in range(65)]
                    if clip!='punch':
                        heights=[q[:,1].min() for q in poses]
                        self.assertGreater(max(heights),.03,'no swing clearance')
                        # All these bipeds author -Z travel and use yaw 180.
                        retreat=[-(poses[i+1][:,2]-poses[i][:,2]).mean() for i in range(64) if max(heights[i:i+2])<.001]
                        self.assertTrue(retreat)
                        self.assertLess(sum(retreat),-.05,'backwards stance')
                        # A correct sign alone still permits visible skating.
                        # At unit runtime travelScale, half a cycle of planted
                        # foot retreat must match walk_speed * elapsed time.
                        if side=='left':
                            definition=pipeline.runtime_definition(sid,Path(tempfile.gettempdir()))
                            settings=model.motion['clips'][clip]
                            duration=max(t['times'][-1] for t in settings['tracks'])
                            distance=(poses[32][:,2]-poses[0][:,2]).mean()
                            speed=distance/(duration*.5/settings['runtime_speed'])
                            self.assertAlmostEqual(speed,definition['locomotion']['walk_speed'],places=4)
                if clip=='punch':
                    for side in ('left','right'):
                        hand=(authored_vertex_owners(model,g,[blob])==names['hand_'+side])&(w[:,0]>.99)
                        self.assertLess(sample(clip,.5)[0][hand,2].mean(),sample(clip,0)[0][hand,2].mean()-.35,'strike fails to reach forward')

    def test_invertebrates_keep_weighted_limbs_stance_and_species_motion(self):
        for sid,count in [('crab',40),('lobster',51),('octopus',35)]:
            recipe=pipeline.load_recipe(sid);model=build_model(recipe)
            g,blob=export_gltf(model,list(model.motion['clips']))
            a=g['meshes'][0]['primitives'][0]['attributes']
            p=accessor(g,[blob],a['POSITION']);v=np.column_stack([p,np.ones(len(p))])
            j=accessor(g,[blob],a['JOINTS_0']).astype(int);w=accessor(g,[blob],a['WEIGHTS_0'])
            inv=accessor(g,[blob],g['skins'][0]['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)
            names={b.name:i for i,b in enumerate(model.bones)}
            self.assertEqual(len(set(j[w>.001])),count)
            @lru_cache(maxsize=None)
            def sample(clip,phase):
                world=world_matrices(sample_nodes(g,[blob],clip,phase))
                palette=np.array([world[n]@iv for n,iv in zip(g['skins'][0]['joints'],inv)])
                return np.einsum('nij,nj->ni',(palette[j]*w[:,:,None,None]).sum(axis=1),v)[:,:3]
            with tempfile.TemporaryDirectory() as temp:
                definition=pipeline.runtime_definition(sid,Path(temp))
            yaw=np.deg2rad(definition['visual']['gltf']['model_yaw_offset_deg'])
            for clip in ('idle','walk')+(('swim',) if sid=='octopus' else ()):
                self.assertTrue(np.allclose(sample(clip,0),sample(clip,1),atol=1e-5),sid+': loop seam')
            for clip in ('idle','walk')+(('punch','die') if sid!='octopus' else ()):
                heights=[sample(clip,i/64)[:,1].min() for i in range(65)]
                self.assertGreater(min(heights),-.002,sid+': sinking')
                self.assertLess(max(heights),.003,sid+': missing contact')
            if sid=='octopus':
                self.assertEqual(definition['visual']['animation']['state_map']['Swim'],'swim')
                for arm in range(8):
                    for k in range(4):
                        bone=model.bones[names[f'arm{arm}_{k}']]
                        self.assertEqual(bone.parent,'body' if k==0 else f'arm{arm}_{k-1}')
                        if k<3:
                            # All coincident cross-section vertices must stay
                            # joined, including the oblique sides of a tube.
                            left=np.flatnonzero(authored_vertex_owners(model,g,[blob])==names[bone.name])
                            right=np.flatnonzero(authored_vertex_owners(model,g,[blob])==names[f'arm{arm}_{k+1}'])
                            pairs=[(u,int(right[np.argmin(np.linalg.norm(p[right]-p[u],axis=1))])) for u in left
                                   if np.linalg.norm(p[right]-p[u],axis=1).min()<1e-6]
                            self.assertTrue(pairs,'no shared arm cross-section')
                            for phase in (.125,.375,.625):
                                q=sample('swim',phase)
                                self.assertLess(max(np.linalg.norm(q[u]-q[v]) for u,v in pairs),1e-5,'arm seam opens')
                    mask=(authored_vertex_owners(model,g,[blob])==names[f'arm{arm}_3'])&(w[:,0]>.99)
                    centres=np.array([sample('swim',i/32)[mask].mean(axis=0) for i in range(33)])
                    self.assertGreater(np.linalg.norm(np.ptp(centres,axis=0)),.01,'frozen arm')
                mantle=(authored_vertex_owners(model,g,[blob])==names['body'])&(w[:,0]>.99)
                self.assertAlmostEqual(np.ptp(sample('swim',.25)[mantle,0])/np.ptp(sample('swim',.75)[mantle,0]),1.03/.97,places=5)
            else:
                self.assertTrue({'idle','walk','punch','die'}.issubset({a['name'] for a in g['animations']}))
                if sid=='crab':
                    # The head faces sideways, while authored +X travel becomes entity +Z.
                    travel=np.array([1.,0.,0.]);forward=-np.sin(yaw)*travel[0]+np.cos(yaw)*travel[2]
                    self.assertGreater(forward,.999)
                    self.assertAlmostEqual(np.cos(yaw),0,places=5)
                for side in ('left','right'):
                    for leg in range(4):
                        self.assertEqual(model.bones[names[f'foot_{side}{leg}']].parent,f'lower_{side}{leg}')
                        mask=(authored_vertex_owners(model,g,[blob])==names[f'foot_{side}{leg}'])&(w[:,0]>.99)
                        poses=[sample('walk',i/64)[mask] for i in range(65)]
                        heights=[q[:,1].min() for q in poses]
                        self.assertGreater(max(heights),.015,'no swing clearance')
                        retreat=[]
                        for i in range(64):
                            if max(heights[i:i+2])<.001:
                                delta=(poses[i+1]-poses[i]).mean(axis=0)
                                retreat.append(-np.sin(yaw)*delta[0]+np.cos(yaw)*delta[2])
                        self.assertTrue(retreat,'no stance')
                        self.assertLess(sum(retreat),-.01,'stance must retract, not moonwalk')
                    # Compare finger separation in palm-local space: closing must
                    # remain visible even when the entire claw lifts to strike.
                    tip=(authored_vertex_owners(model,g,[blob])==names['moving_finger_'+side])&(w[:,0]>.99)&np.isclose(p[:,2],-19/16)
                    fixed=(authored_vertex_owners(model,g,[blob])==names['palm_'+side])&(w[:,0]>.99)&np.isclose(p[:,2],-19/16)
                    gaps=[np.linalg.norm(sample('punch',t)[tip].mean(axis=0)-sample('punch',t)[fixed].mean(axis=0)) for t in (0,.5)]
                    self.assertGreater(gaps[0],gaps[1]+.015,'pincer does not close')
        invalid=copy.deepcopy(pipeline.load_recipe('crab'));invalid['locomotion_axis']='+y'
        with patch.object(pipeline,'load_recipe',return_value=invalid):
            with tempfile.TemporaryDirectory() as temp,self.assertRaises(ValueError):pipeline.runtime_definition('crab',Path(temp))

    def test_sloped_planform_contact_matches_exported_vertices(self):
        recipe=copy.deepcopy(pipeline.load_recipe('crab'))
        shape={'planform':[[0,0],[1,0],[0,1],[1,1]],'planform_y':[[.5,.5,0,0],[1,1,.5,.5]]}
        recipe['anatomy']={'bones':[{'name':'body','pivot':[0,4,0],'cubes':[
            {'name':'probe','origin':[-2,0,-2],'size':[4,8,4],'role':'limb','shape':shape}]}]}
        ts=[0,.5,1]
        recipe['motion']={'clips':{'idle':{'tracks':[{'bone':'body','path':'rotation','times':ts,'values':[[0,0,0]]*3}]},
            'die':{'tracks':[{'bone':'body','path':'translation','times':ts,'values':[[0,.25,0]]*3},
                {'bone':'body','path':'rotation','times':ts,'values':[[0,0,0],[20,0,40],[0,0,90]]}],
                'contact':{'root':'body','bones':['body']}}}}
        model=build_model(recipe);g,blob=export_gltf(model,['die'])
        p=accessor(g,[blob],g['meshes'][0]['primitives'][0]['attributes']['POSITION'])
        inv=accessor(g,[blob],g['skins'][0]['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)[0]
        for phase in (0,.5,1):
            world=world_matrices(sample_nodes(g,[blob],'die',phase))[0]
            posed=np.column_stack([p,np.ones(len(p))])@(world@inv).T
            self.assertAlmostEqual(float(posed[:,1].min()),0,places=6)
        for heights in ([[0]*4,[0]*4],[[0]*4,[1,1,float('nan'),1]],[[-.1]*4,[1]*4]):
            shape['planform_y']=heights
            with self.assertRaises(ValueError):build_model(recipe)

    def test_large_aquatic_rigs_move_weighted_chains_and_keep_loop_seams(self):
        for sid,count in [('whale',14),('water_dragon',15)]:
            model=build_model(pipeline.load_recipe(sid));g,blob=export_gltf(model,list(model.motion['clips']))
            a=g['meshes'][0]['primitives'][0]['attributes']
            p=accessor(g,[blob],a['POSITION']);v=np.column_stack([p,np.ones(len(p))])
            j=accessor(g,[blob],a['JOINTS_0']).astype(int);w=accessor(g,[blob],a['WEIGHTS_0'])
            inv=accessor(g,[blob],g['skins'][0]['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)
            names={b.name:i for i,b in enumerate(model.bones)}
            self.assertEqual(len(set(j[w>.001])),count)
            @lru_cache(maxsize=None)
            def sample(clip,phase):
                world=world_matrices(sample_nodes(g,[blob],clip,phase))
                palette=np.array([world[n]@iv for n,iv in zip(g['skins'][0]['joints'],inv)])
                return np.einsum('nij,nj->ni',(palette[j]*w[:,:,None,None]).sum(axis=1),v)[:,:3]
            for clip in ('idle','walk','run'):
                self.assertTrue(np.allclose(sample(clip,0),sample(clip,1),atol=1e-5),sid+': loop seam')
            if sid=='whale':
                mask=(authored_vertex_owners(model,g,[blob])==names['fluke_left'])&(w[:,0]>.99)
                centres=np.array([sample('walk',i/32)[mask].mean(axis=0) for i in range(33)])
                self.assertGreater(np.ptp(centres[:,1]),.2,'whale flukes must beat vertically')
                self.assertLess(np.ptp(centres[:,0]),.005)
                self.assertGreater(np.ptp(sample('run',.25)[:,0]),np.ptp(p[:,0])*1.15,'long flippers must unfold')
            else:
                self.assertIn('punch',{a['name'] for a in g['animations']})
                dims=np.ptp(p,axis=0)
                self.assertGreater(dims[1],dims[2]*1.8,'serpent must keep its tall original silhouette')
                jaw=(authored_vertex_owners(model,g,[blob])==names['jaw'])&(w[:,0]>.99)
                self.assertLess(sample('punch',.5)[jaw,1].mean(),sample('punch',0)[jaw,1].mean()-.015)
                # Every shared vertical ring of the curved/tapered neck stays
                # closed under full skinning, including yaw and pitch.
                for lower,upper,y in [('body','neck_base',23),('neck_base','neck_middle',31),('neck_middle','neck_upper',35)]:
                    left=(authored_vertex_owners(model,g,[blob])==names[lower])&np.isclose(p[:,1],y/16)
                    right=(authored_vertex_owners(model,g,[blob])==names[upper])&np.isclose(p[:,1],y/16)
                    self.assertTrue(left.any() and right.any())
                    for phase in (.125,.375,.625):
                        posed=sample('run',phase)
                        for point in posed[left]:
                            self.assertLess(np.linalg.norm(posed[right]-point,axis=1).min(),1e-5,'serpent neck crack')

    def test_marine_mammals_preserve_propulsion_contact_and_closed_necks(self):
        for sid,count,required in [('manatee',15,{'idle','fly','punch','die'}),
                                   ('seal',16,{'idle','walk','fly','die'})]:
            model=build_model(pipeline.load_recipe(sid));g,blob=export_gltf(model,list(model.motion['clips']))
            a=g['meshes'][0]['primitives'][0]['attributes']
            p=accessor(g,[blob],a['POSITION']);v=np.column_stack([p,np.ones(len(p))])
            j=accessor(g,[blob],a['JOINTS_0']).astype(int);w=accessor(g,[blob],a['WEIGHTS_0'])
            inv=accessor(g,[blob],g['skins'][0]['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)
            names={b.name:i for i,b in enumerate(model.bones)}
            self.assertEqual(len(set(j[w>.001])),count)
            self.assertEqual({a['name'] for a in g['animations']},required)
            @lru_cache(maxsize=None)
            def sample(clip,phase):
                world=world_matrices(sample_nodes(g,[blob],clip,phase))
                palette=np.array([world[n]@iv for n,iv in zip(g['skins'][0]['joints'],inv)])
                return np.einsum('nij,nj->ni',(palette[j]*w[:,:,None,None]).sum(axis=1),v)[:,:3]
            for clip in ('idle','fly')+ (('walk',) if sid=='seal' else ()):
                self.assertTrue(np.allclose(sample(clip,0),sample(clip,1),atol=1e-5),sid+': loop seam')
            for clip in ('die',)+ (('walk',) if sid=='seal' else ()):
                heights=[sample(clip,float(phase))[:,1].min() for phase in np.linspace(0,1,65)]
                self.assertGreaterEqual(min(heights),-.002,sid+': sinking')
                self.assertLess(max(heights),.01,sid+': floating contact')
            tip=names['paddle_tip' if sid=='manatee' else 'hindflipper_tip_left']
            mask=(authored_vertex_owners(model,g,[blob])==tip)&(w[:,0]>.99)
            centres=np.array([sample('fly',i/32)[mask].mean(axis=0) for i in range(33)])
            if sid=='manatee':
                self.assertGreater(np.ptp(centres[:,1]),.2,'manatee paddle must beat vertically')
                self.assertLess(np.ptp(centres[:,0]),.003)
                paddle=p[authored_vertex_owners(model,g,[blob])==tip]
                self.assertGreater(np.ptp(paddle[:,0]),np.ptp(paddle[:,1])*8,'paddle must be wide and flat')
            else:
                self.assertGreater(np.ptp(centres[:,0]),.15,'seal hind flippers must sweep laterally')
                self.assertLess(np.ptp(centres[:,1]),.003)
                head=(authored_vertex_owners(model,g,[blob])==names['head'])&(w[:,0]>.99)
                heights=[sample('walk',i/32)[head,1].mean() for i in range(33)]
                self.assertGreater(np.ptp(heights),.02,'land locomotion must undulate the body')
            # At a moving head/neck joint, coincident bind vertices on both
            # sides must remain coincident after full exported skinning.
            plane=-9/16 if sid=='manatee' else -11/16
            neck=(authored_vertex_owners(model,g,[blob])==names['neck'])&np.isclose(p[:,2],plane)
            head=(authored_vertex_owners(model,g,[blob])==names['head'])&np.isclose(p[:,2],plane)
            self.assertTrue(neck.any() and head.any())
            for phase in (.125,.375,.625):
                posed=sample('idle',phase)
                for point in posed[neck]:
                    self.assertLess(np.linalg.norm(posed[head]-point,axis=1).min(),1e-5,sid+': neck crack')
            with tempfile.TemporaryDirectory() as temp:
                definition=pipeline.runtime_definition(sid,Path(temp))
                self.assertEqual(definition['visual']['animation']['state_map']['Swim'],'fly')

    def test_aquatic_fish_preserve_anatomy_and_species_propulsion(self):
        for sid,count in [('shark',20),('seahorse',17)]:
            model=build_model(pipeline.load_recipe(sid))
            g,blob=export_gltf(model,list(model.motion['clips']))
            a=g['meshes'][0]['primitives'][0]['attributes']
            p=accessor(g,[blob],a['POSITION']);v=np.column_stack([p,np.ones(len(p))])
            j=accessor(g,[blob],a['JOINTS_0']).astype(int);w=accessor(g,[blob],a['WEIGHTS_0'])
            inv=accessor(g,[blob],g['skins'][0]['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)
            names={b.name:i for i,b in enumerate(model.bones)}
            self.assertEqual(len(set(j[w>.001])),count)
            def sample(clip,phase):
                world=world_matrices(sample_nodes(g,[blob],clip,phase))
                palette=np.array([world[n]@iv for n,iv in zip(g['skins'][0]['joints'],inv)])
                return np.einsum('nij,nj->ni',(palette[j]*w[:,:,None,None]).sum(axis=1),v)[:,:3]
            for clip in ('idle','fly'):
                self.assertTrue(np.allclose(sample(clip,0),sample(clip,1),atol=1e-5))
            for phase in np.linspace(0,1,65):
                self.assertGreaterEqual(sample('die',float(phase))[:,1].min(),-.002,sid)
                self.assertLess(sample('die',float(phase))[:,1].min(),.015,sid)
            if sid=='shark':
                self.assertEqual({a['name'] for a in g['animations']},{'idle','walk','fly','punch','die'})
                mask=(authored_vertex_owners(model,g,[blob])==names['caudal_upper'])&(w[:,0]>.99)
                centres=np.array([sample('fly',i/32)[mask].mean(axis=0) for i in range(33)])
                self.assertGreater(np.ptp(centres[:,0]),.2,'tail must beat laterally')
                self.assertLess(np.ptp(centres[:,1]),.005,'shark must not use a dolphin tail beat')
                upper=p[(authored_vertex_owners(model,g,[blob])==names['caudal_upper']),1]
                lower=p[(authored_vertex_owners(model,g,[blob])==names['caudal_lower']),1]
                self.assertGreater(np.ptp(upper),np.ptp(lower)*1.5)
                jaw=(authored_vertex_owners(model,g,[blob])==names['jaw'])&(w[:,0]>.99)
                self.assertLess(sample('punch',.5)[jaw,1].mean(),sample('punch',0)[jaw,1].mean()-.04)
            else:
                rest=sample('idle',0);dims=np.ptp(rest,axis=0)
                self.assertGreater(dims[1],dims[2]*1.4,'seahorse must remain upright')
                moving={g['nodes'][c['target']['node']]['name'] for a in g['animations'] if a['name']=='fly' for c in a['channels']}
                self.assertTrue(all('tail'+str(i) in moving for i in range(7)))
                self.assertTrue({'dorsal0','dorsal1','pectoral_left','pectoral_right'}.issubset(moving))
                # A real opening in the side silhouette of the curled tail.
                q=np.array([4.5,3.5])/16
                for face in g['meshes'][0]['primitives']:
                    indices=accessor(g,[blob],face['indices']).astype(int).ravel()
                    for tri in indices.reshape(-1,3):
                        a,b,c=p[tri][:,1:3];matrix=np.column_stack([b-a,c-a])
                        if abs(np.linalg.det(matrix))<1e-8:continue
                        u,t=np.linalg.solve(matrix,q-a)
                        self.assertFalse(u>=0 and t>=0 and u+t<=1,'curled tail filled its opening')

    def test_side_profile_is_validated_and_contact_uses_exported_shape(self):
        recipe=copy.deepcopy(pipeline.load_recipe('shark'))
        cube=next(b for b in recipe['anatomy']['bones'] if b['name']=='dorsal0')['cubes'][0]
        cube['shape']['side_profile']=[[0,0],[1,1],[0,1],[1,0]]
        with self.assertRaises(ValueError):build_model(recipe)
        recipe=copy.deepcopy(pipeline.load_recipe('shark'))
        cube=next(b for b in recipe['anatomy']['bones'] if b['name']=='dorsal0')['cubes'][0]
        cube['shape']['side_profile'][0][0]=float('nan')
        with self.assertRaises(ValueError):build_model(recipe)
        recipe=copy.deepcopy(pipeline.load_recipe('water_dragon'))
        recipe['anatomy']['bones'][0]['cubes'][0]['shape']['side_width']=[0,1]
        with self.assertRaises(ValueError):build_model(recipe)
        recipe=copy.deepcopy(pipeline.load_recipe('water_dragon'))
        recipe['anatomy']={'bones':[{'name':'body','pivot':[0,4,0],'cubes':[
            {'name':'probe','origin':[-2,0,-2],'size':[4,8,4],'role':'body',
             'shape':{'side_profile':[[0,0],[1,.4],[0,1],[1,.9]],'side_width':[.25,1]}}]}]}
        ts=[0,.5,1]
        recipe['motion']={'clips':{'die':{'tracks':[
            {'bone':'body','path':'translation','times':ts,'values':[[0,.25,0]]*3},
            {'bone':'body','path':'rotation','times':ts,'values':[[0,0,0],[0,0,48],[0,0,90]]}],
            'contact':{'root':'body','bones':['body']}}}}
        recipe['motion']['clips']['idle']={'tracks':[{'bone':'body','path':'rotation','times':ts,'values':[[0,0,0]]*3}]}
        model=build_model(recipe);g,blob=export_gltf(model,['die'])
        a=g['meshes'][0]['primitives'][0]['attributes'];p=accessor(g,[blob],a['POSITION'])
        inv=accessor(g,[blob],g['skins'][0]['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)[0]
        for phase in (0,.5,1):
            world=world_matrices(sample_nodes(g,[blob],'die',phase))[0]
            posed=np.column_stack([p,np.ones(len(p))])@(world@inv).T
            self.assertAlmostEqual(float(posed[:,1].min()),0,places=6)

    def test_puffin_and_wasp_have_functional_weighted_anatomy(self):
        for sid,count,required,loop in [('puffin',18,{'idle','fly','die'},'fly'),
                                        ('wasp',29,{'idle','walk','punch','die'},'walk')]:
            model=build_model(pipeline.load_recipe(sid))
            g,blob=export_gltf(model,list(model.motion['clips']))
            self.assertEqual(len(model.bones),count)
            self.assertTrue(required.issubset({a['name'] for a in g['animations']}))
            a=g['meshes'][0]['primitives'][0]['attributes']
            p=accessor(g,[blob],a['POSITION']);v=np.column_stack([p,np.ones(len(p))])
            j=accessor(g,[blob],a['JOINTS_0']).astype(int);w=accessor(g,[blob],a['WEIGHTS_0'])
            self.assertEqual(len(set(j[w>.001])),count, sid+': unweighted helper counted as anatomy')
            inv=accessor(g,[blob],g['skins'][0]['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)
            @lru_cache(maxsize=None)
            def sample(clip,phase):
                world=world_matrices(sample_nodes(g,[blob],clip,phase))
                palette=np.array([world[n]@iv for n,iv in zip(g['skins'][0]['joints'],inv)])
                return np.einsum('nij,nj->ni',(palette[j]*w[:,:,None,None]).sum(axis=1),v)[:,:3]
            self.assertTrue(np.allclose(sample(loop,0),sample(loop,1),atol=1e-5),sid+': loop seam')
            for clip in (('walk','die') if sid=='puffin' else ('die',)):
                for phase in np.linspace(0,1,65):
                    self.assertGreaterEqual(sample(clip,float(phase))[:,1].min(),-.002,sid+': below floor')
            bones={b.name:b for b in model.bones}
            moving={g['nodes'][c['target']['node']]['name'] for a in g['animations'] if a['name']==loop for c in a['channels']}
            for side in ('left','right'):
                if sid=='puffin':
                    self.assertEqual(bones['wing_primary_'+side].parent,'wing_lower_'+side)
                    self.assertEqual(bones['wing_lower_'+side].parent,'wing_upper_'+side)
                    self.assertTrue(all('wing_'+part+'_'+side in moving for part in ('upper','lower','primary')))
                else:
                    for i in range(3):
                        self.assertEqual(bones[f'leg_lower_{side}{i}'].parent,f'leg_upper_{side}{i}')
                        self.assertIn(f'leg_lower_{side}{i}',moving)
                    self.assertIn('forewing_'+side,moving);self.assertIn('hindwing_'+side,moving)
                    self.assertEqual(bones['antenna_tip_'+side].parent,'antenna_base_'+side)
            if sid=='puffin':
                folded=np.ptp(sample('idle',0)[:,0])
                spans=[np.ptp(sample('fly',i/16)[:,0]) for i in range(17)]
                self.assertGreater(max(spans),folded*2)
                self.assertGreater(max(spans)-min(spans),.15)
            else:
                tip=next(i for i,b in enumerate(model.bones) if b.name=='abdomen_tip')
                mask=(authored_vertex_owners(model,g,[blob])==tip)&(w[:,0]>.99)
                self.assertLess(sample('punch',.5)[mask,2].mean(),sample('punch',0)[mask,2].mean()-.1,
                                'stinger must advance toward authored -Z front')

    def test_bird_states_reference_real_authored_clips(self):
        with tempfile.TemporaryDirectory() as temp:
            owl=pipeline.runtime_definition('owl',Path(temp))['visual']['animation']
            self.assertEqual(owl['state_map']['Fly'],'fly')
            self.assertEqual(owl['state_map']['Glide'],'glide')
            self.assertTrue(owl['clips']['fly']['loop'])
            self.assertEqual(owl['clips']['idle']['speed'],1)
            recipe=copy.deepcopy(pipeline.load_recipe('owl'))
            recipe['animation_state_map']['Fly']='missing_clip'
            with patch.object(pipeline,'load_recipe',return_value=recipe):
                with self.assertRaises(ValueError):pipeline.runtime_definition('owl',Path(temp))
            recipe=copy.deepcopy(pipeline.load_recipe('owl'))
            # Use an unpublished clip: glide becomes an existing runtime clip
            # after publication, so its loop flag can legitimately be inherited.
            recipe['motion']['clips']['unpublished_test_clip']=copy.deepcopy(recipe['motion']['clips']['glide'])
            del recipe['motion']['clips']['unpublished_test_clip']['runtime_loop']
            with patch.object(pipeline,'load_recipe',return_value=recipe):
                with self.assertRaises(ValueError):pipeline.runtime_definition('owl',Path(temp))

    def test_bird_skinning_spreads_wings_and_separates_land_from_swim(self):
        for sid in ('owl','penguin','puffin'):
            model=build_model(pipeline.load_recipe(sid))
            g,blob=export_gltf(model,['idle','walk']+(['fly'] if sid=='penguin' else []))
            a=g['meshes'][0]['primitives'][0]['attributes']
            p=accessor(g,[blob],a['POSITION']);v=np.column_stack([p,np.ones(len(p))])
            j=accessor(g,[blob],a['JOINTS_0']).astype(int);w=accessor(g,[blob],a['WEIGHTS_0'])
            inv=accessor(g,[blob],g['skins'][0]['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)
            @lru_cache(maxsize=None)
            def sample(clip,phase):
                world=world_matrices(sample_nodes(g,[blob],clip,phase))
                palette=np.array([world[n]@iv for n,iv in zip(g['skins'][0]['joints'],inv)])
                matrices=(palette[j]*w[:,:,None,None]).sum(axis=1)
                return np.einsum('nij,nj->ni',matrices,v)[:,:3]
            for phase in range(65):
                self.assertGreaterEqual(sample('walk',phase/64)[:,1].min(),-.01,sid+': foot below floor')
            self.assertTrue(np.allclose(sample('walk',0),sample('walk',1),atol=1e-5))
            with tempfile.TemporaryDirectory() as temp:
                yaw=pipeline.runtime_definition(sid,Path(temp))['visual']['gltf']['model_yaw_offset_deg']
            for side in ('left','right'):
                foot=next(i for i,b in enumerate(model.bones) if b.name=='foot_'+side)
                mask=(authored_vertex_owners(model,g,[blob])==foot)&(w[:,0]>.99)
                retreat=[]
                for phase in range(64):
                    pa,pb=phase/64,(phase+1)/64
                    qa=sample_nodes(g,[blob],'walk',pa)[foot]['rotation']
                    qb=sample_nodes(g,[blob],'walk',pb)[foot]['rotation']
                    if np.linalg.norm(qa[:3])<1e-5 and np.linalg.norm(qb[:3])<1e-5:
                        retreat.append(float((sample('walk',pb)[mask,2]-sample('walk',pa)[mask,2]).mean())*np.cos(np.deg2rad(yaw)))
                self.assertTrue(retreat,sid+': no planted bird foot')
                self.assertLess(sum(retreat),-.005,sid+': backwards bird step')
            if sid=='owl':
                folded=np.ptp(sample('idle',0)[:,0])
                spans=[np.ptp(sample('fly',p/16)[:,0]) for p in range(17)]
                self.assertGreater(max(spans),folded*2)
                self.assertGreater(max(spans)-min(spans),.2)
                self.assertGreater(np.ptp(sample('glide',0)[:,0]),folded*2)
            elif sid=='penguin':
                land=np.ptp(sample('idle',0),axis=0)
                swim=np.ptp(sample('fly',.25),axis=0)
                self.assertGreater(land[1],land[2]*1.2)
                self.assertGreater(swim[2],swim[1]*2)

    def test_mammal_skinning_keeps_ground_contact_and_loop_seams(self):
        for sid in ('rat','kitten','hedgehog','panda','warthog'):
            model=build_model(pipeline.load_recipe(sid))
            names={b.name:i for i,b in enumerate(model.bones)}
            required=['idle','walk','run']+(['punch'] if sid in ('rat','panda','warthog') else [])+(['die'] if sid=='warthog' else [])
            g,blob=export_gltf(model,required)
            self.assertTrue(set(required).issubset({a['name'] for a in g['animations']}))
            attrs=g['meshes'][0]['primitives'][0]['attributes']
            positions=accessor(g,[blob],attrs['POSITION'])
            vertices=np.column_stack([positions,np.ones(len(positions))])
            joints=accessor(g,[blob],attrs['JOINTS_0']).astype(int)
            weights=accessor(g,[blob],attrs['WEIGHTS_0'])
            self.assertTrue(np.any(np.sum(weights>.01,axis=1)>1),sid)
            self.assertTrue(np.allclose(weights.sum(axis=1),1),sid)
            inverse=accessor(g,[blob],g['skins'][0]['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)
            @lru_cache(maxsize=None)
            def nodes(clip,phase):return sample_nodes(g,[blob],clip,phase)
            @lru_cache(maxsize=None)
            def sample(clip,phase):
                world=world_matrices(nodes(clip,phase))
                palette=np.array([world[j]@inv for j,inv in zip(g['skins'][0]['joints'],inverse)])
                matrices=(palette[joints]*weights[:,:,None,None]).sum(axis=1)
                return np.einsum('nij,nj->ni',matrices,vertices)[:,:3]
            for clip in ('walk','run','punch'):
                if clip not in required:
                    continue
                if clip!='punch':
                    self.assertTrue(np.allclose(sample(clip,0),sample(clip,1),atol=1e-5),sid+': loop seam')
                for limb in ('fl','fr','bl','br'):
                    mask=(authored_vertex_owners(model,g,[blob])==names['lower_'+limb])&(weights[:,0]>.99)
                    self.assertTrue(mask.any(),sid+': no paw vertices')
                    heights=[sample(clip,phase/64)[mask,1].min() for phase in range(64)]
                    self.assertGreaterEqual(min(heights),-.01,sid+': sinking '+limb)
                    if clip!='punch':
                        self.assertGreater(max(heights),.015,sid+': frozen '+limb)
                        # On a planted knee, the foot retracts relative to the
                        # entity's +Z forward. A reversed swing/stance loop
                        # passes floor checks but otherwise moonwalks.
                        with tempfile.TemporaryDirectory() as temp:
                            yaw=pipeline.runtime_definition(sid,Path(temp))['visual']['gltf']['model_yaw_offset_deg']
                        retreat=[]
                        for phase in range(64):
                            pa,pb=phase/64,(phase+1)/64
                            qa=nodes(clip,pa)[names['lower_'+limb]]['rotation']
                            qb=nodes(clip,pb)[names['lower_'+limb]]['rotation']
                            if np.linalg.norm(qa[:3])<1e-5 and np.linalg.norm(qb[:3])<1e-5:
                                retreat.append(float((sample(clip,pb)[mask,2]-sample(clip,pa)[mask,2]).mean())*np.cos(np.deg2rad(yaw)))
                        self.assertTrue(retreat,sid+': no stance interval')
                        self.assertLess(sum(retreat),-.005,sid+': planted foot travels forward')

    def test_contact_correction_preserves_authored_horizontal_lunge(self):
        recipe=copy.deepcopy(pipeline.load_recipe('panda'))
        track=next(t for t in recipe['motion']['clips']['punch']['tracks'] if t['path']=='translation')
        for i,v in enumerate(track['values']):
            v[0]=i*.04;v[2]=-.1*i
        expected=[(v[0],v[2]) for v in track['values']]
        authored_times=list(track['times'])
        model=build_model(recipe)
        result=next(t for t in model.motion['clips']['punch']['tracks'] if t['path']=='translation')
        # A denser root sampler may add times, but must preserve the same
        # horizontal trajectory rather than comparing channel array lengths.
        expected=np.column_stack([np.interp(result['times'],authored_times,
            np.array(expected)[:,axis]) for axis in range(2)])
        self.assertTrue(np.allclose(np.array(result['values'])[:,[0,2]],expected,atol=1e-12))

    def test_death_contact_uses_shaped_geometry_and_animated_scale(self):
        recipe=copy.deepcopy(pipeline.load_recipe('warthog'))
        times=recipe['motion']['clips']['die']['tracks'][0]['times']
        recipe['motion']['clips']['die']['tracks'].append(dict(bone='body',path='scale',times=times,
            values=[[1-.08*i/(len(times)-1),1+.05*i/(len(times)-1),1] for i in range(len(times))]))
        model=build_model(recipe);g,blob=export_gltf(model,['idle','walk','run','punch','die'])
        a=g['meshes'][0]['primitives'][0]['attributes']
        p=accessor(g,[blob],a['POSITION']);v=np.column_stack([p,np.ones(len(p))])
        j=accessor(g,[blob],a['JOINTS_0']).astype(int);w=accessor(g,[blob],a['WEIGHTS_0'])
        inv=accessor(g,[blob],g['skins'][0]['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)
        for phase in range(65):
            world=world_matrices(sample_nodes(g,[blob],'die',phase/64))
            palette=np.array([world[n]@iv for n,iv in zip(g['skins'][0]['joints'],inv)])
            matrices=(palette[j]*w[:,:,None,None]).sum(axis=1)
            posed=np.einsum('nij,nj->ni',matrices,v)[:,:3]
            self.assertGreaterEqual(posed[:,1].min(),-.01,'corpse below floor')
            self.assertLess(posed[:,1].min(),.02,'corpse floating above floor')

    def test_cat_export_preserves_pointed_ears_and_rising_tail(self):
        model=build_model(pipeline.load_recipe('kitten'))
        g,blob=export_gltf(model,['idle','walk','run'])
        attrs=g['meshes'][0]['primitives'][0]['attributes']
        pos=accessor(g,[blob],attrs['POSITION'])
        joints=accessor(g,[blob],attrs['JOINTS_0']).astype(int)
        for name in ('ear_left','ear_right'):
            index=next(i for i,b in enumerate(model.bones) if b.name==name)
            ear=pos[authored_vertex_owners(model,g,[blob])==index]
            top=ear[np.isclose(ear[:,1],ear[:,1].max())]
            base=ear[np.isclose(ear[:,1],ear[:,1].min())]
            self.assertLess(np.ptp(top[:,0]),np.ptp(base[:,0])*.2)
        for name in ('tail0','tail1'):
            index=next(i for i,b in enumerate(model.bones) if b.name==name)
            segment=pos[authored_vertex_owners(model,g,[blob])==index]
            start=segment[np.isclose(segment[:,2],segment[:,2].min())]
            end=segment[np.isclose(segment[:,2],segment[:,2].max())]
            self.assertGreater(end[:,1].min()-start[:,1].min(),.04)

    def test_aquatic_wave_keeps_attached_tentacles_and_stable_surfaces(self):
        for sid in ('trout','squid','dolphin'):
            self.assertEqual(overlaps(build_model(pipeline.load_recipe(sid))),[],sid)
        model=build_model(pipeline.load_recipe('squid'))
        bones={b.name:b for b in model.bones}
        for i in range(8):
            arm=bones[f'tentacle{i}']; tip=bones[f'tip{i}']
            self.assertEqual(arm.parent,'body')
            self.assertEqual(tip.parent,arm.name)
            self.assertEqual(arm.cubes[0].origin[1],tip.cubes[0].origin[1]+tip.cubes[0].size[1])
            self.assertEqual(tip.pivot[1],arm.cubes[0].origin[1])

    def test_authored_gait_parameters_are_part_of_runtime_definition(self):
        with tempfile.TemporaryDirectory() as temp:
            definition = pipeline.runtime_definition('spider', Path(temp))
            self.assertEqual(definition['visual']['animation']['leg_swing_deg'], 12)
            self.assertEqual(definition['visual']['animation']['body_bob_blocks'], 0)
            original = pipeline._read_json(pipeline.MODELS/'spider'/'creature.json')
            for key in ('bounds', 'locomotion', 'eye_height', 'habitat'):
                self.assertEqual(definition[key], original[key])
            recipe=copy.deepcopy(pipeline.load_recipe('spider'))
            recipe['animation_overrides']['walk_speed']=20
            with patch.object(pipeline, 'load_recipe', return_value=recipe):
                with self.assertRaises(ValueError):
                    pipeline.runtime_definition('spider', Path(temp))
            turtle=pipeline.runtime_definition('tortoise',Path(temp))
            self.assertEqual(turtle['visual']['animation_profile'],'turtle')
            warthog=pipeline.runtime_definition('warthog',Path(temp))
            self.assertEqual(warthog['visual']['animation']['clips']['die']['speed'],1)
            bad_speed=copy.deepcopy(pipeline.load_recipe('warthog'))
            bad_speed['motion']['clips']['die']['runtime_speed']=0
            with patch.object(pipeline,'load_recipe',return_value=bad_speed):
                with self.assertRaises(ValueError):
                    pipeline.runtime_definition('warthog',Path(temp))
            recipe=copy.deepcopy(pipeline.load_recipe('tortoise'))
            recipe['animation_profile']='silent_fallback_typo'
            with patch.object(pipeline,'load_recipe',return_value=recipe):
                with self.assertRaises(ValueError):
                    pipeline.runtime_definition('tortoise',Path(temp))

    def test_coat_wave_has_no_unstable_same_facing_coplanar_surfaces(self):
        for sid in ('pig','sheep','wolf','fox'):
            self.assertEqual(overlaps(build_model(pipeline.load_recipe(sid))),[],sid)

    def test_hermitcrab_preserves_functional_chains_and_death_clip(self):
        model=build_model(pipeline.load_recipe('hermitcrab'))
        bones={b.name:b for b in model.bones}
        for side in ('l','r'):
            for i in range(3):
                self.assertEqual(bones[f'foot_{side}{i}'].parent,f'lower_{side}{i}')
                self.assertEqual(bones[f'lower_{side}{i}'].parent,f'upper_{side}{i}')
            for i in range(2):
                self.assertIn(f'carrier_{side}{i}_1',bones)
            self.assertEqual(bones[f'moving_finger_{side}'].parent,f'palm_{side}')
        g,_=export_gltf(model,['idle','walk','die'])
        self.assertEqual({a['name'] for a in g['animations']},{'idle','walk','die'})
        self.assertLessEqual(len(model.bones),96)

    def test_skin_is_bound_to_the_base_geometry_and_uses_its_atlas(self):
        with tempfile.TemporaryDirectory() as temp, patch.object(pipeline, 'STAGING', Path(temp)):
            stage=pipeline.build_skin('human_guard')
            original=bundle_hash(stage)
            recipe=copy.deepcopy(pipeline.load_recipe('human'))
            recipe['anatomy']['bones'][1]['cubes'][0]['size'][0]+=1
            with patch.object(pipeline, 'load_recipe', return_value=recipe):
                pipeline.build_skin('human_guard')
            self.assertNotEqual(original,bundle_hash(stage))

    def test_humanoid_retains_hand_anchors_and_first_person_uv_contract(self):
        for sid in ("human", "zombie"):
            model = build_model(pipeline.load_recipe(sid))
            bones = {b.name:b for b in model.bones}
            for side, sign in (("right", -1), ("left", 1)):
                self.assertEqual(bones[side+"Item"].parent, side+"Arm")
                self.assertEqual(bones[side+"Item"].pivot, [sign*6, 15, 1])
            arm = next(c for c in model.cubes() if c.name == "right_sleeve")
            self.assertEqual(arm.uv, (40, 16))
            self.assertEqual(arm.size, [4, 12, 4])
            rectangles=[]
            for cube in model.cubes():
                u,v=cube.uv;w,h,d=cube.size
                rect=(u,v,u+2*(w+d),v+h+d)
                for other in rectangles:
                    self.assertFalse(rect[0]<other[2] and rect[2]>other[0] and rect[1]<other[3] and rect[3]>other[1])
                rectangles.append(rect)

    def test_ray_keeps_full_clip_capability_and_distributed_wing_chain(self):
        model=build_model(pipeline.load_recipe("stingray"))
        self.assertEqual(len(model.bones), 24)
        g,blob=export_gltf(model, ["idle", "fly", "punch", "die"])
        self.assertEqual({a['name'] for a in g['animations']}, {"idle", "fly", "punch", "die"})
        fly=next(a for a in g['animations'] if a['name']=='fly')
        moving={g['nodes'][ch['target']['node']]['name'] for ch in fly['channels']}
        self.assertTrue(all(f'wing_{s}{i}' in moving for s in ('l','r') for i in range(8)))
        self.assertGreater(len(blob),0)

    def test_rigid_wings_fins_and_tail_rotate_at_authored_roots(self):
        for sid in ('rigid_demo_flyer','rigid_demo_swimmer'):
            parts=export_rigid(build_model(pipeline.load_recipe(sid)))['parts']
            for part in parts:
                if part['id'].startswith(('wing','fin','tail')):
                    self.assertIn('pivot',part)
                    self.assertNotEqual(part['pivot'],part['offset'])

    def test_full_skin_contact_preserves_authored_motion_with_independent_keys(self):
        for sid in ('badger','hedgehog','kitten','panda','penguin','rat','warthog'):
            recipe=pipeline.load_recipe(sid);raw=copy.deepcopy(recipe)
            for clip in raw['motion']['clips'].values():clip.pop('contact',None)
            model=build_model(recipe);raw_model=build_model(raw)
            g,blob=export_gltf(model,list(model.motion['clips']));ref,ref_blob=export_gltf(raw_model,list(raw_model.motion['clips']))
            a=g['meshes'][0]['primitives'][0]['attributes'];p=accessor(g,[blob],a['POSITION']);v=np.column_stack([p,np.ones(len(p))])
            j=accessor(g,[blob],a['JOINTS_0']).astype(int);w=accessor(g,[blob],a['WEIGHTS_0'])
            inverse=accessor(g,[blob],g['skins'][0]['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)
            def skin(doc,data,clip,phase):
                world=world_matrices(sample_nodes(doc,[data],clip,phase));palette=np.array([world[n]@iv for n,iv in zip(doc['skins'][0]['joints'],inverse)])
                return np.einsum('nij,nj->ni',(palette[j]*w[:,:,None,None]).sum(1),v)[:,:3]
            for clip in model.motion['clips']:
                if clip in ('fly','swim','glide'):continue
                before={(t['bone'],t['path']):t for t in raw_model.motion['clips'][clip]['tracks']}
                for t in model.motion['clips'][clip]['tracks']:
                    key=(t['bone'],t['path'])
                    if key==('body','translation'):
                        authored=recipe['motion']['clips'][clip]
                        if 'contact'in authored:self.assertEqual(len(t['times']),authored['contact']['samples'])
                        else:
                            self.assertIn('ground_contact_baked',authored)
                            self.assertEqual(t,before[key],'explicit baked root must remain unchanged')
                    else:self.assertEqual(t,before[key],'contact must not replace authored joint keys')
                for phase in np.linspace(0,1,513):
                    q=skin(g,blob,clip,float(phase))*recipe['model_scale']
                    self.assertGreaterEqual(q[:,1].min(),-.001,'full mesh penetrates floor between root keys')
                    self.assertLess(q[:,1].min(),.001,'full mesh loses support')
                for phase in [.013,.127,.263,.419,.631,.887]:
                    q=skin(g,blob,clip,phase);original=skin(ref,ref_blob,clip,phase)
                    delta=q-original
                    self.assertTrue(np.allclose(delta[:,[0,2]],0,atol=1e-6),'contact changes horizontal motion')
                    self.assertLess(np.ptp(delta[:,1]),1e-5,'contact changes deformation instead of root height')


    def test_badger_uses_segmented_legs_and_blended_trunk(self):
        recipe = pipeline.load_recipe("badger")
        model = build_model(recipe)
        names = {b.name for b in model.bones}
        for leg in ("fl", "fr", "bl", "br"):
            self.assertIn("upper_"+leg, names)
            self.assertIn("lower_"+leg, names)
        g, blob = export_gltf(model, ["idle", "walk", "run", "punch"])
        index = g["meshes"][0]["primitives"][0]["attributes"]["WEIGHTS_0"]
        accessor = g["accessors"][index]
        view = g["bufferViews"][accessor["bufferView"]]
        offset = view.get("byteOffset", 0)+accessor.get("byteOffset", 0)
        values = [struct.unpack_from("<4f", blob, offset+16*i) for i in range(accessor["count"])]
        self.assertTrue(any(sum(w>.01 for w in weights)>1 for weights in values))
        for weights in values:
            self.assertAlmostEqual(sum(weights), 1, places=5)

    def test_missing_authored_clip_never_becomes_generic_motion(self):
        model = build_model(pipeline.load_recipe("badger"))
        with self.assertRaisesRegex(ValueError, "Missing authored clip"):
            export_gltf(model, ["idle", "swim"])

    def test_pilot_atlases_fit_every_box_uv_island(self):
        for species in ("cow", "badger", "rigid_demo_walker"):
            model = build_model(pipeline.load_recipe(species))
            for cube in model.cubes():
                self.assertLessEqual(cube.uv[0]+2*(cube.size[0]+cube.size[2]), model.texture_size[0])
                self.assertLessEqual(cube.uv[1]+cube.size[1]+cube.size[2], model.texture_size[1])

    def test_rigid_part_ids_remain_unique_for_multicube_bones(self):
        parts = export_rigid(build_model(pipeline.load_recipe("rigid_demo_walker")))["parts"]
        self.assertEqual(len(parts), len({p["id"] for p in parts}))
        self.assertTrue({"leg_fl", "leg_fr", "leg_bl", "leg_br"}.issubset({p["id"] for p in parts}))

    def test_rigid_authored_pivot_policy_preserves_accepted_walker_tail(self):
        recipe=pipeline.load_recipe('rigid_demo_walker')
        model=build_model(recipe)
        parts=export_rigid(model,center_pivot_bones=recipe['rigid_center_pivot_bones'])['parts']
        tail=next(p for p in parts if p['id']=='tail')
        self.assertNotIn('pivot',tail)
        self.assertTrue(all('pivot' in p for p in parts if p.get('limb')=='leg'))
        with self.assertRaisesRegex(ValueError,'Unknown rigid center-pivot'):
            export_rigid(model,center_pivot_bones=['missing'])

    def test_emit_preserves_an_artist_edited_recipe_by_default(self):
        recipe = copy.deepcopy(pipeline.load_recipe("cow"))
        recipe["body"][0] += 3
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            path = folder/"cow.yaml"
            path.write_text(yaml.safe_dump(recipe), encoding="utf-8")
            before = path.read_bytes()
            with patch.object(pipeline, "RECIPES", folder), patch.object(pipeline, "CATALOG", folder/"catalog.yaml"), patch.object(pipeline, "SPECIES", {"cow":pipeline.SPECIES["cow"]}):
                pipeline.emit_recipes()
            self.assertEqual(before, path.read_bytes())

    def test_cyclic_authored_rig_is_rejected(self):
        recipe = copy.deepcopy(pipeline.load_recipe("cow"))
        recipe["anatomy"]["bones"][0]["parent"] = "head"
        with self.assertRaisesRegex(ValueError, "cyclic"):
            build_model(recipe)


if __name__ == "__main__":
    unittest.main()
