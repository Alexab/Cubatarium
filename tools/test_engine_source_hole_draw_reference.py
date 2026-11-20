import copy
import json
import math
import unittest

from engine_source_draw_reference import evaluate_records
from engine_rgb_reference import COLORS
import test_engine_rgb_reference as rgb_tests


class SourceHoleDrawTest(unittest.TestCase):
    def setUp(self):
        fixture=rgb_tests.RgbReferenceTest(); fixture.setUp(); self.addCleanup(fixture.doCleanups)
        self.scene=fixture.scene
        self.scene.update(schema='engine-smoke-fixture.v1',
            air_border_fixture=dict(schema='engine-positive-air-border-fixture.v1'))
        self.scene['cave_bounds']=dict(floor=-48.5,ceiling=-16.5,z_low=34.5,z_high=67.5)
        self.scene['lighting_fixture']['emitters']=[[x,-49,51] for x in (0,8,16,24,32)]
        self.logs=[fixture.log]; self.rows=[]
        self.witnesses=[]
        targets=[([x,-48.5,51],4) for x in range(16,27) if x!=22]
        targets += [([22,-49.5,51],4),([22.5,-49.1,51],3),([21.5,-49.1,51],1),
                    ([22,-49.1,51.5],2),([22,-49.1,50.5],0)]
        for phase,frames in ((0,(1,2,3)),(1,(5,6,7))):
            for frame in frames:
                for w in fixture.witnesses[:2]:
                    witness=copy.deepcopy(w);witness['frame_epoch']=frame
                    self.witnesses.append(witness)
                for index,(target,face) in enumerate(targets):
                    hole=index>=10
                    origin=[22,-45.5,51] if hole else [target[0],-45.5,51]
                    direction=[(b-a)/math.dist(origin,target) for a,b in zip(origin,target)]
                    if hole and phase:
                        depth=math.dist(origin,target);level=11;material=560
                    else:
                        depth=(-48.5-origin[1])/direction[1]
                        x=22 if hole else target[0]
                        emitters=(0,8,16,22,24,32) if not phase else (0,8,16,24,32)
                        level=14-min(abs(x-e) for e in emitters)
                        material=376 if x in emitters else 560
                        face=4
                    # Closed-form full-day RGB, independent of the evaluator.
                    color=COLORS['glowstone' if material==376 else 'stone']
                    rgba=[round(c*(.005+.345*level/15)) for c in color[:3]]+[255]
                    packed=sum(v<<s for v,s in zip(rgba,(24,16,8,0)))
                    row=copy.deepcopy(fixture.rows[0])
                    row.update(frame_epoch=frame,renderer_pixel_opaque_hit_distance=depth,
                        renderer_pixel_opaque_vertex_light_block_id=material,
                        renderer_pixel_opaque_vertex_light_face_index=face,
                        renderer_pixel_pretransparent_rgba=packed,renderer_pixel_rgba=packed,
                        renderer_pixel_opaque_world_epoch=4,renderer_pixel_opaque_drawable=1,
                        renderer_pixel_opaque_draw_ready=1,renderer_pixel_opaque_preview_shader_override=0,
                        renderer_pixel_opaque_bound_field=dict(schema='bound-section-light-pixel.v1',
                            requested=True,captured=True,valid=True,cpu_matches=True,packed=level<<4,
                            program=1,atlas_texture=2,table_texture=3,handle=[0,0,1,0],frame_epoch=frame,
                            world_epoch=4,field_incarnation=11,field_revision=0,
                            geometry_incarnation=2,geometry_content=4))
                    for c,o,d in zip('xyz',origin,direction):
                        row['renderer_pixel_ray_origin_'+c]=o
                        row['renderer_pixel_camera_pos_'+c]=o
                        row['renderer_pixel_ray_direction_'+c]=d
                    self.rows.append(row)
        self.write_log()

    def write_log(self):
        self.logs[0].write_text(''.join('[RgbReferenceMaterial] '+json.dumps(w)+'\n'
                                      for w in self.witnesses),encoding='utf-8')

    def check(self,status):
        result=evaluate_records(self.scene,self.rows,self.logs,action_frame=4,world_epoch=4,
            cell=[22,-49,51],expected='glowstone',replacement='air',initial_extra_emitters=[[22,-49,51]])
        self.assertEqual(result['status'],status,result)
        return result

    def hole_rows(self):
        return [r for r in self.rows if r['frame_epoch']>4 and
                r['renderer_pixel_ray_origin_x']==22]

    def test_five_faces_and_before_after_transport_have_independent_rgb(self):
        result=self.check('PASS')
        self.assertEqual(result['field_hole_faces'],list(range(5)))
        self.assertEqual(result['rgb_hole_faces'],list(range(5)))

    def test_stale_source_field_and_naive_opaque_shortcut_fail_on_each_face(self):
        for row in self.hole_rows()[:5]:
            original=copy.deepcopy(row)
            for level in (13,14):
                row['renderer_pixel_opaque_bound_field']['packed']=level<<4
                self.check('FAIL')
            row.clear();row.update(original)

    def test_stale_top_geometry_cannot_be_reclassified_as_ordinary_floor(self):
        row=self.hole_rows()[0]
        row.update(renderer_pixel_opaque_hit_distance=3,
                   renderer_pixel_opaque_vertex_light_block_id=376)
        result=self.check('FAIL')
        self.assertEqual(result['counts']['after']['geometry_faults'],1)

    def test_incorrect_rgb_with_correct_gpu_field_fails(self):
        row=self.hole_rows()[1]
        for key in ('renderer_pixel_rgba','renderer_pixel_pretransparent_rgba'):
            original=row[key];row[key]=255;self.check('FAIL');row[key]=original

    def test_self_consistent_wrong_field_and_rgb_still_fail(self):
        row=self.hole_rows()[2]
        row['renderer_pixel_opaque_bound_field']['packed']=13<<4
        rgba=[round(c*(.005+.345*13/15)) for c in COLORS['stone'][:3]]+[255]
        row['renderer_pixel_rgba']=row['renderer_pixel_pretransparent_rgba']=sum(
            v<<s for v,s in zip(rgba,(24,16,8,0)))
        self.check('FAIL')

    def test_missing_bound_and_missing_face_do_not_pass_from_floor_coverage(self):
        row=self.hole_rows()[0]
        row['renderer_pixel_opaque_bound_field'].update(captured=False,valid=False)
        self.check('UNTESTED')
        self.rows=[r for r in self.rows if not(r['frame_epoch']>4 and
                    r['renderer_pixel_opaque_vertex_light_face_index']==0)]
        result=self.check('UNTESTED')
        self.assertIn('after_gpu_hole_face_0',result['missing_coverage'])
        self.assertIn('after_rgb_hole_face_0',result['missing_coverage'])

    def test_same_frame_material_gpu_world_and_shader_proof_are_required(self):
        row=self.hole_rows()[0]
        for key,value in (('world_epoch',99),('frame_epoch',99)):
            old=row['renderer_pixel_opaque_bound_field'][key]
            row['renderer_pixel_opaque_bound_field'][key]=value
            self.check('FAIL');row['renderer_pixel_opaque_bound_field'][key]=old
        row['renderer_pixel_opaque_preview_shader_override']=1
        self.check('UNTESTED');row['renderer_pixel_opaque_preview_shader_override']=0
        self.witnesses=[w for w in self.witnesses if w['frame_epoch']!=5]
        self.write_log();self.check('UNTESTED')

    def test_no_before_evidence_and_unordered_action_frame_cannot_pass(self):
        self.rows=[r for r in self.rows if r['frame_epoch']>4]
        self.check('UNTESTED')
        self.rows[0]['frame_epoch']=4
        # Deliberately corrupt its binding; same-action paint is unordered and
        # excluded, never borrowed to fill either phase's coverage.
        self.rows[0]['renderer_pixel_opaque_bound_field']['packed']=0
        result=self.check('UNTESTED')
        self.assertEqual(result['unordered_action_rows'],1)

    def test_authored_pack_and_actual_material_witness_are_checked(self):
        self.witnesses[-1]['variation']=1
        self.write_log();self.check('FAIL')
        self.witnesses[-1]['variation']=0;self.write_log()
        from pathlib import Path
        pack=Path(self.scene['rgb_fixture']['pack_path'])
        (pack/'blocks/stone.json').write_text('{}',encoding='utf-8')
        self.assertEqual(self.check('FAIL')['reason'],'rgb_fixture_pack_changed')
