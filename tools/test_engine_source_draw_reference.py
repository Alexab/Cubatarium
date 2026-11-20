import copy
import json
import unittest

import test_engine_rgb_reference as rgb_tests
from engine_source_draw_reference import evaluate_records


class SourceDrawTest(unittest.TestCase):
    def setUp(self):
        fixture=rgb_tests.RgbReferenceTest()
        fixture.setUp()
        self.addCleanup(fixture.doCleanups)
        self.scene=fixture.scene
        self.scene['cave_bounds'].update(floor=-48.5,ceiling=-16.5,z_low=34.5,z_high=67.5)
        self.scene['lighting_fixture']['emitters']=[[0,-49,51],[8,-49,51]]
        self.rows=[]
        self.logs=[fixture.log]
        witnesses=[]
        for phase in (0,1):
            for frame in (1,2,3):
                epoch=frame+4*phase
                for w in fixture.witnesses:
                    if w['frame_epoch']==frame:
                        w=copy.deepcopy(w);w['frame_epoch']=epoch;witnesses.append(w)
                for base in fixture.rows:
                    if base['frame_epoch']!=frame: continue
                    r=copy.deepcopy(base)
                    x=r['renderer_pixel_ray_origin_x']
                    sources=(0,4,8) if phase else (0,8)
                    level=14-min(abs(x-e) for e in sources)
                    color=rgb_tests.COLORS['glowstone' if x in sources else 'stone']
                    rgba=[round(c*(.005+.345*level/15)) for c in color[:3]]+[255]
                    packed=sum(v<<s for v,s in zip(rgba,(24,16,8,0)))
                    r.update(frame_epoch=epoch,renderer_pixel_ray_origin_y=-45.5,
                        renderer_pixel_camera_pos_y=-45.5,
                        renderer_pixel_rgba=packed,renderer_pixel_pretransparent_rgba=packed,
                        renderer_pixel_opaque_vertex_light_block_id=376 if x in sources else 560,
                        renderer_pixel_opaque_world_epoch=4,renderer_pixel_opaque_vertex_light_valid=1,
                        renderer_pixel_opaque_drawable=1,renderer_pixel_opaque_draw_ready=1,
                        renderer_pixel_opaque_vertex_light_distance=0,renderer_pixel_opaque_vertex_light_preview=0,
                        renderer_pixel_opaque_preview_shader_override=0,
                        renderer_pixel_opaque_packed_light_payload_valid=1,
                        renderer_pixel_opaque_packed_light_quad_count=1,
                        renderer_pixel_opaque_bound_field=dict(schema='bound-section-light-pixel.v1',
                            requested=True,captured=True,valid=True,cpu_matches=True,packed=level<<4,
                            program=1,atlas_texture=2,table_texture=3,handle=[0,0,1,0],
                            frame_epoch=epoch,world_epoch=4,field_incarnation=11,field_revision=0,
                            geometry_incarnation=2,geometry_content=4))
                    self.rows.append(r)
        fixture.log.write_text(''.join('[RgbReferenceMaterial] '+json.dumps(w)+'\n' for w in witnesses),encoding='utf-8')
        self.expected='stone';self.target='glowstone';self.extra=[]

    def check(self,status):
        r=evaluate_records(self.scene,self.rows,self.logs,action_frame=4,world_epoch=4,
            cell=[4,-49,51],expected=self.expected,replacement=self.target,initial_extra_emitters=self.extra)
        self.assertEqual(r['status'],status,r)
        return r

    def test_addition_joins_independent_rgb_and_actual_gpu_values(self):
        self.check('PASS')

    def test_removal_reverses_expected_model_without_observed_truth(self):
        self.expected,self.target='glowstone','stone';self.extra=[[4,-49,51]]
        for r in self.rows:
            frame=r['frame_epoch']
            r['frame_epoch']=frame+4 if frame<4 else frame-4
            r['renderer_pixel_opaque_bound_field']['frame_epoch']=r['frame_epoch']
        self.check('PASS')

    def test_stale_gpu_after_action_fails_even_with_correct_rgb(self):
        self.rows[34]['renderer_pixel_opaque_bound_field']['packed']=10<<4
        self.check('FAIL')

    def test_zero_rgb_fails_even_with_correct_bound_field(self):
        self.rows[34]['renderer_pixel_rgba']=255
        self.check('FAIL')

    def test_bound_field_cannot_hide_behind_missing_baked_vertex_proximity(self):
        self.rows[34]['renderer_pixel_opaque_bound_field']['packed']=10<<4
        self.rows[34]['renderer_pixel_opaque_vertex_light_distance']=1
        r=self.check('FAIL')
        self.assertEqual(r['counts']['after']['field_faults'],1)

    def test_missing_capture_is_incomplete_not_numeric_success(self):
        self.rows[34]['renderer_pixel_opaque_bound_field'].update(captured=False,valid=False)
        self.check('UNTESTED')

    def test_provisional_surface_cannot_supply_joined_gpu_acceptance(self):
        self.rows[34]['renderer_pixel_opaque_preview_shader_override']=1
        self.check('UNTESTED')

    def test_foreign_world_and_frame_cannot_supply_binding(self):
        for key in ('frame_epoch','world_epoch'):
            old=copy.deepcopy(self.rows[34])
            self.rows[34]['renderer_pixel_opaque_bound_field'][key]=99
            self.check('FAIL');self.rows[34]=old

    def test_air_hole_requires_another_geometry_model(self):
        self.target='air';self.check('UNTESTED')

    def test_mdi_field_requires_actual_command_geometry_and_instanced_row(self):
        e=self.rows[34]['renderer_pixel_opaque_bound_field']
        e.update(draw_table=True,draw_table_buffer=4,draw_table_row=2,
                 indirect_buffer=5,indirect_command=[6,1,0,0,2],geometry_command=[6,0,0])
        self.check('PASS')
        original=copy.deepcopy(e)
        for change in (dict(draw_table_buffer=0),dict(indirect_buffer=0),
                       dict(draw_table_row=3),dict(indirect_command=[6,0,0,0,2]),
                       dict(indirect_command=[6,1,0,1,2]),dict(geometry_command=[7,0,0]),
                       dict(draw_table=1)):
            with self.subTest(change=change):
                e.update(change);self.check('FAIL');e.clear();e.update(copy.deepcopy(original))

    def test_missing_before_effect_cannot_pass_from_after_only(self):
        self.rows=self.rows[30:]
        self.check('UNTESTED')
