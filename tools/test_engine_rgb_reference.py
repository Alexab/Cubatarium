import copy
import json
import math
from pathlib import Path
import tempfile
import unittest

from engine_rgb_reference import COLORS,evaluate,prepare,shade_dry_floor


class RgbReferenceTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.world=self.root/'worlds/EngineSmoke_rgbtest'
        self.world.mkdir(parents=True)
        (self.world/'world_data.json').write_text(json.dumps(dict(resource_packs=dict(
            primary=['cubatarium_worldgen_cc0'],secondary=[]))),encoding='utf-8')
        original=self.root/'resource_packs/cubatarium_worldgen_cc0/blocks'
        original.mkdir(parents=True)
        for name,identifier,emission in [('stone',560,0),('glowstone',376,15)]:
            (original/(name+'.json')).write_text(json.dumps(dict(name=name,id=identifier,
                lighting=dict(emission=emission),render=dict(texture_variation=1))),encoding='utf-8')
        self.scene=prepare(dict(name='dry-negative-cave',expected_eye_in_fluid=False,
            chunk_x_bounds=[-4,3],cave_bounds=dict(floor=-43.5,ceiling=-30.5,z_low=44.5,z_high=57.5),
            lighting_fixture=dict(schema='engine-positive-blocklight-fixture.v1',material_id=376,
                                  emission=15,emitters=[[0,-44,51],[8,-44,51]])),self.world)
        self.perf=self.root/'perf.jsonl'; self.log=self.root/'native.log'
        self.rows=[]; self.witnesses=[]
        for frame in (1,2,3):
            for material in self.scene['rgb_fixture']['materials']:
                self.witnesses.append(dict(schema='rgb-reference-material.v1',frame_epoch=frame,
                    material_id=material['id'],valid=True,rgba_min=material['rgba'],rgba_max=material['rgba'],
                    variation=0,emission=material['emission'],opaque_cube=True,frames=1,
                    stems=[material['stem']]*6,linear_texture=True,framebuffer_srgb=False))
            for x in range(10):
                level=14-min(abs(x),abs(x-8))
                # At full day, min ambient.12, sky0: block brightness is
                # .005+(.35-.005)*level/15. Closed-form values are independent
                # of the reference implementation's output.
                color=COLORS['glowstone' if x in (0,8) else 'stone']
                intensity=.005+.345*level/15
                rgba=[round(c*intensity) for c in color[:3]]+[255]
                packed=sum(v<<s for v,s in zip(rgba,[24,16,8,0]))
                self.rows.append(dict(kind='renderer_pixel_probe',frame_epoch=frame,
                    renderer_pixel_ray_origin_x=x,renderer_pixel_ray_origin_y=-40.5,renderer_pixel_ray_origin_z=51,
                    renderer_pixel_ray_direction_x=0,renderer_pixel_ray_direction_y=-1,renderer_pixel_ray_direction_z=0,
                    renderer_pixel_camera_pos_x=x,renderer_pixel_camera_pos_y=-40.5,renderer_pixel_camera_pos_z=51,
                    renderer_pixel_opaque_hit_distance=3,renderer_pixel_marker_visible=2,
                    renderer_pixel_opaque_surface_valid=1,renderer_pixel_opaque_vertex_light_block_id=376 if x in (0,8) else 560,
                    renderer_pixel_opaque_vertex_light_face_index=4,renderer_pixel_opaque_light_source_kind=2,
                    renderer_pixel_pretransparent_rgba=packed,renderer_pixel_rgba=packed,
                    renderer_pixel_shader_min_ambient=.12,renderer_pixel_shader_day_factor=1,
                    renderer_pixel_shader_night_factor=0,renderer_pixel_shader_sky_scale=1,
                    renderer_pixel_shader_precipitation=0,renderer_pixel_shader_wetness=0,
                    renderer_pixel_shader_light_debug_mode=0,renderer_pixel_fog_state_valid=1,
                    renderer_pixel_fog_enabled=0,renderer_pixel_air_fog_enabled=0,
                    renderer_pixel_fog_horizontal=0,renderer_pixel_underwater_fog_enabled=0,
                    renderer_pixel_underwater_fog_submerged=0))

    def check(self):
        self.perf.write_text(''.join(json.dumps(r)+'\n' for r in self.rows),encoding='utf-8')
        self.log.write_text(''.join('[RgbReferenceMaterial] '+json.dumps(r)+'\n' for r in self.witnesses),encoding='utf-8')
        return evaluate(self.scene,self.perf,[self.log])

    def test_normal_color_with_positive_light(self):
        result=self.check()
        self.assertEqual(result['status'],'PASS',result)
        self.assertEqual(result['checked'],30)

    def test_black_zeroed_light_and_wrong_bound_texture_fail(self):
        for packed in (255,0x101010ff,0xff00ffff):
            for key in ('renderer_pixel_pretransparent_rgba','renderer_pixel_rgba'):
                with self.subTest(packed=packed,key=key):
                    old=self.rows[0][key]; self.rows[0][key]=packed
                    self.assertEqual(self.check()['status'],'FAIL')
                    self.rows[0][key]=old

    def test_observed_light_does_not_set_reference(self):
        self.rows[0]['renderer_pixel_opaque_vertex_block_light']=0
        self.rows[0]['renderer_pixel_opaque_live_face_light_packed']=0
        self.assertEqual(self.check()['status'],'PASS')
        self.rows[0]['renderer_pixel_rgba']=255
        self.assertEqual(self.check()['status'],'FAIL')

    def test_missing_surface_cannot_disappear_from_coverage(self):
        self.rows[0]['renderer_pixel_opaque_surface_valid']=0
        self.rows[0]['renderer_pixel_opaque_hit_distance']=-1
        self.assertEqual(self.check()['status'],'FAIL')

    def test_material_witness_is_required_in_same_frame(self):
        original=copy.deepcopy(self.witnesses)
        self.witnesses=[]
        self.assertEqual(self.check()['status'],'UNTESTED')
        self.witnesses=original
        self.witnesses[0]['frame_epoch']=4
        self.assertEqual(self.check()['status'],'UNTESTED')

    def test_wrong_atlas_catalog_or_debug_render_fails(self):
        for key,value in [('variation',1),('emission',15),('opaque_cube',False),('linear_texture',False),
                          ('framebuffer_srgb',True),('valid',False),('frames',2),('rgba_min',[0,0,0,255])]:
            with self.subTest(key=key):
                old=copy.deepcopy(self.witnesses[0]); self.witnesses[0][key]=value
                self.assertEqual(self.check()['status'],'FAIL')
                self.witnesses[0]=old
        self.rows[0]['renderer_pixel_shader_light_debug_mode']=1
        self.assertEqual(self.check()['status'],'FAIL')

    def test_fog_closed_form_and_rain_are_colored(self):
        row=copy.deepcopy(self.rows[0])
        row.update(renderer_pixel_air_fog_enabled=1,renderer_pixel_fog_start=0,renderer_pixel_fog_end=6,
            renderer_pixel_fog_density=1,renderer_pixel_fog_env_multiplier=1,
            renderer_pixel_fog_min_blend=0,renderer_pixel_fog_color_r=.2,
            renderer_pixel_fog_color_g=.4,renderer_pixel_fog_color_b=.6)
        rgb,_,fog=shade_dry_floor(row,[192,128,64,255],15,3,0)
        self.assertEqual(fog,.5)
        for actual,expected in zip(rgb,[(192*.35+51)/2,(128*.35+102)/2,(64*.35+153)/2,255]):
            self.assertAlmostEqual(actual,expected)
        row['renderer_pixel_shader_precipitation']=.8
        rain,_,_=shade_dry_floor(row,[192,128,64,255],15,3,0)
        self.assertNotEqual(rain,rgb)

    def test_changed_pack_is_a_failure(self):
        pack=Path(self.scene['rgb_fixture']['pack_path'])
        (pack/'blocks/stone.json').write_text('{}',encoding='utf-8')
        self.assertEqual(self.check()['reason'],'rgb_fixture_pack_changed')

    def test_sparse_or_unrequested_coverage_cannot_pass(self):
        self.rows=self.rows[:3]
        self.assertEqual(self.check()['status'],'UNTESTED')
        self.rows=[]
        self.assertEqual(self.check()['status'],'UNTESTED')


if __name__=='__main__': unittest.main()
