import copy
import json
from pathlib import Path
import tempfile
import unittest

from engine_floor_light_reference import evaluate


class BoundFloorLightTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.path=Path(self.temp.name)/'trace.jsonl'
        self.scene=dict(name='dry-negative-cave',chunk_x_bounds=[-5,3],rgb_fixture=dict(requested=True),
            cave_bounds=dict(floor=-43.5,ceiling=-30.5,z_low=44.5,z_high=57.5),
            lighting_fixture=dict(schema='engine-positive-blocklight-fixture.v1',emission=15,
                                  material_id=376,emitters=[[0,-44,51]]))
        p='renderer_pixel_opaque_';self.rows=[]
        for i in range(30):
            x=i%10
            r=dict(kind='renderer_pixel_probe',frame_epoch=i//10+1,
                renderer_pixel_ray_origin_x=x,renderer_pixel_ray_origin_y=-36,
                renderer_pixel_ray_origin_z=51,renderer_pixel_ray_direction_x=0,
                renderer_pixel_ray_direction_y=-1,renderer_pixel_ray_direction_z=0)
            r.update({p+'hit_distance':7.5,p+'surface_valid':1,p+'vertex_light_valid':1,
                p+'vertex_light_face_index':4,p+'drawable':1,p+'draw_ready':1,p+'source_index_count':0,
                p+'vertex_light_distance':0,p+'vertex_light_preview':0,p+'preview_shader_override':0,
                p+'vertex_block_light':0,p+'vertex_sky_light':0,p+'vertex_light_block_id':376 if x==0 else 560,
                p+'light_source_kind':2,p+'packed_light_payload_valid':1,p+'packed_light_quad_count':8,
                p+'world_epoch':4,
                p+'bound_field':dict(schema='bound-section-light-pixel.v1',requested=True,captured=True,
                    valid=True,cpu_matches=True,packed=(14-x)<<4,program=9,atlas_texture=11,table_texture=12,
                    handle=[0,2,3,0],world_epoch=4,field_incarnation=81,field_revision=0,
                    geometry_incarnation=15,geometry_content=7)})
            self.rows.append(r)
    def check(self):
        self.path.write_text(''.join(json.dumps(r)+'\n' for r in self.rows),encoding='utf-8')
        return evaluate(self.scene,self.path)
    def test_actual_bound_positive_survives_zero_baked_vertices(self):
        result=self.check();self.assertEqual(result['status'],'PASS',result)
        self.assertEqual(result['source_counts']['bound_gpu'],30)
        self.assertEqual(result['positive_bound_with_baked_zero'],30)
    def test_zero_stale_missing_or_malformed_binding_cannot_pass(self):
        for key,value in [('packed',0),('captured',False),('valid',False),('cpu_matches',False),
            ('world_epoch',5),('program',0),('handle',[0,0,0,0]),('field_incarnation',0),
            ('field_revision',-1),('atlas_texture',True),('packed',256)]:
            with self.subTest(key=key,value=value):
                original=copy.deepcopy(self.rows[0]['renderer_pixel_opaque_bound_field'])
                self.rows[0]['renderer_pixel_opaque_bound_field'][key]=value
                self.assertNotEqual(self.check()['status'],'PASS')
                self.rows[0]['renderer_pixel_opaque_bound_field']=original
    def test_missing_requested_readback_never_uses_good_baked_light(self):
        for row in self.rows:
            field=row.pop('renderer_pixel_opaque_bound_field')
            row['renderer_pixel_opaque_vertex_block_light']=(field['packed']>>4)/15
        result=self.check();self.assertEqual(result['status'],'UNTESTED')
        self.assertEqual(result['missing_bound_fields'],30)
    def test_bound_readback_is_compared_to_independent_scene(self):
        for row in self.rows:
            row['renderer_pixel_opaque_bound_field']['packed']=0
            row['renderer_pixel_opaque_vertex_block_light']=0
            row['renderer_pixel_opaque_live_face_light_packed']=0
        result=self.check();self.assertEqual(result['status'],'FAIL')
        self.assertEqual(result['faults'],30)


if __name__=='__main__':unittest.main()
