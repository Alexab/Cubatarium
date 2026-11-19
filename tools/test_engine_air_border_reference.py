import copy
import json
from pathlib import Path
import tempfile
import unittest

from engine_camera_light_reference import evaluate
from engine_smoke_fixtures import prepare_lit_negative_cave


class AirBorderReferenceTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.scene=dict(name='dry-negative-cave',expected_eye_in_fluid=False,chunk_x_bounds=[-32,3],
            cave_bounds=dict(floor=-48.5,ceiling=-16.5,z_low=34.5,z_high=67.5),
            air_border_fixture=dict(schema='engine-positive-air-border-fixture.v1',
                                    source_section_y=-4,target_section_y=-3),
            lighting_fixture=dict(schema='engine-positive-blocklight-fixture.v1',emission=15,
                material_id=376,emitters=[[cx*16+offset,-49,51] for cx in range(-32,4) for offset in (0,8)]))
    def check(self,change='',omit=-1):
        rows=[]
        for i in range(30):
            x=-(i%10)*16+i%4
            block=6-i%4
            rows.append(f'[CameraLightSample] world_epoch=4 frame_epoch={i} cell=({x},-40,51) '
                f'coord=({x//16},-3,3) physical=1 resident=0 ready=1 sky=0 block_light={block} '
                f'field_incarnation=12 '+(change if i==0 else '')+'\n' if i!=omit else '')
            if i==0:rows.append('[FirstWorldFrameWitness] '+json.dumps(dict(phase='first_world_paint',world_epoch=4))+'\n')
        p=self.root/'native.log';p.write_text(''.join(rows),encoding='utf-8')
        return evaluate(self.scene,[p],expected_frames=30)
    def test_cross_section_positive_without_geometry_every_paint(self):
        r=self.check();self.assertEqual(r['status'],'PASS',r)
        self.assertEqual(r['air_samples'],30)
        self.assertEqual(r['expected_block_levels'],[3,4,5,6])
    def test_zero_light_phantom_geometry_unknown_and_stale_fail(self):
        for change in ['block_light=0','sky=15','resident=1','ready=0','field_incarnation=0',
                       'coord=(0,-4,3)','physical=0']:
            with self.subTest(change=change):self.assertNotEqual(self.check(change)['status'],'PASS')
        self.assertEqual(self.check(omit=10)['status'],'UNTESTED')
    def test_source_in_wrong_section_rejects_reference(self):
        self.scene['lighting_fixture']['emitters'][0][1]=-48
        self.assertEqual(self.check()['status'],'FAIL')
    def test_real_prepared_target_sections_are_absent_and_source_is_on_border(self):
        world=self.root/'worlds/EngineSmoke_border';(world/'chunks').mkdir(parents=True)
        (world/'world_data.json').write_text(json.dumps(dict(procedural=dict(min_height=-64,max_height=319),
            resource_packs=dict(primary=['cubatarium_worldgen_cc0'],secondary=[]))),encoding='utf-8')
        (world/'users.json').write_text(json.dumps(dict(Username={})),encoding='utf-8')
        block=self.root/'resource_packs/cubatarium_worldgen_cc0/blocks';block.mkdir(parents=True)
        (block/'glowstone.json').write_text(json.dumps(dict(name='glowstone',id=376,
            lighting=dict(emission=15),render=dict(style='cube'))),encoding='utf-8')
        scene=prepare_lit_negative_cave(world,1,air_border=True)
        self.assertEqual(scene['starting_eye'],[22.0,-46.88,51.0])
        self.assertEqual(scene['route_rise'],6)
        lo,hi=scene['chunk_x_bounds']
        for cx in range(lo,hi+1):
            for cy in (-3,-2):
                self.assertFalse((world/'chunks'/f'{cx}_{cy}_3.json').exists())
                self.assertFalse((world/'chunks'/f'{cx}_{cy}_3.cchunk').exists())
            floor=json.loads((world/'chunks'/f'{cx}_-4_3.json').read_text(encoding='utf-8'))
            emitters=[v for v in floor['voxels'] if v['type']=='glowstone']
            self.assertEqual({(v['lx'],v['ly'],v['lz']) for v in emitters},{(0,15,3),(8,15,3)})


if __name__=='__main__':unittest.main()
