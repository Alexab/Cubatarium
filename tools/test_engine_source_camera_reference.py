import copy
import json
import unittest
from engine_source_camera_reference import evaluate_records


class SourceCameraReferenceTest(unittest.TestCase):
    def setUp(self):
        self.scene = dict(expected_eye_in_fluid=False, chunk_x_bounds=[-32, 3],
            cave_bounds=dict(floor=-48.5, ceiling=-16.5, z_low=34.5, z_high=67.5),
            air_border_fixture=dict(schema='engine-positive-air-border-fixture.v1'),
            lighting_fixture=dict(schema='engine-positive-blocklight-fixture.v1',
                emission=15, material_id=376, emitters=[[24,-49,51]]))
        self.plan = dict(schema='engine-native-world-actions.v1', entries=[
            dict(cell=[22,-49,51], expected='stone', replacement='glowstone')])
        self.action = dict(schema='engine-world-actions-observation.v1', api='UWorld::ReplaceBlockAt',
            started=True, failed=False, storage_matches_isolated_world=True,
            observations=[dict(phase='initial',cell=[22,-49,51],frame_epoch=20,success=True,
                               expected_id=560,target_id=376,before_id=560,after_id=376,api_called=True,api_success=True),
                          dict(phase='final',cell=[22,-49,51],frame_epoch=40,success=True,
                               expected_id=560,target_id=376,before_id=376,after_id=376,api_called=False,api_success=False)])
        self.material = dict(schema='engine-world-actions-evidence.v1',status='PASS',
            cells=[dict(cell=[22,-49,51],before_id=560,after_id=376)])
        self.extra=[]
        self.lights=[11 if i<20 else 13 for i in range(1,41)]
        self.changes={}

    def lines(self):
        out=[]
        for frame,light in enumerate(self.lights,1):
            fields=dict(world_epoch=4,frame_epoch=frame,cell='(22,-47,51)',coord='(1,-3,3)',
                        physical=1,resident=0,ready=1,sky=0,block_light=light,field_incarnation=11)
            fields.update(self.changes.get(frame,{}))
            out.append('[CameraLightSample] '+' '.join(f'{k}={v}' for k,v in fields.items()))
            if frame==1:
                out.append('[FirstWorldFrameWitness] '+json.dumps(dict(phase='first_world_paint',ready=True,world_epoch=4)))
        return out

    def check(self,status,lines=None):
        result=evaluate_records(self.scene,self.lines() if lines is None else lines,self.action,self.plan,
            self.material,expected_frames=40,initial_extra_emitters=self.extra)
        self.assertEqual(result['status'],status,result)
        return result

    def test_addition_changes_same_cell_independently(self):
        r=self.check('PASS')
        self.assertEqual(r['transitions'],[dict(cell=(22,-47,51),before=[11],after=[13])])

    def removal(self):
        self.extra=[[22,-49,51]]
        self.plan['entries'][0].update(expected='glowstone',replacement='air')
        self.action['observations'][0].update(expected_id=376,target_id=0,before_id=376,after_id=0)
        self.action['observations'][1].update(expected_id=376,target_id=0,before_id=0,after_id=0)
        self.material['cells'][0].update(before_id=376,after_id=0)
        self.lights=[13 if i<20 else 11 for i in range(1,41)]

    def test_removal_retires_emission_without_using_measured_expected_light(self):
        self.removal()
        self.check('PASS')

    def test_old_source_light_after_removal_is_failure(self):
        self.removal();self.lights[24]=13
        self.check('FAIL')

    def test_missing_added_source_is_failure(self):
        self.lights[24]=11;self.check('FAIL')

    def test_unready_paint_is_incomplete_not_acceptance(self):
        self.changes[25]=dict(ready=0,field_incarnation=0)
        r=self.check('UNTESTED');self.assertIn('no_pending_fields',r['missing_coverage'])

    def test_no_material_proof_cannot_inherit_numeric_success(self):
        self.material['status']='UNTESTED';self.check('UNTESTED')

    def test_wrong_action_binding_fails(self):
        self.material['cells'][0]['cell']=[23,-49,51];self.check('FAIL')

    def test_missing_action_epoch_cannot_infer_time_from_brightness(self):
        del self.action['observations'][0]['frame_epoch'];self.check('UNTESTED')

    def test_boolean_air_id_is_not_a_material_identity(self):
        self.removal();self.action['observations'][0]['after_id']=False;self.check('FAIL')

    def test_missing_paint_is_not_complete(self):
        lines=self.lines();del lines[26];self.check('UNTESTED',lines)

    def test_duplicate_or_reordered_frame_is_malformed(self):
        lines=self.lines();lines.insert(25,copy.deepcopy(lines[24]));self.check('FAIL',lines)

    def test_resident_sky_wrong_world_and_invalid_identity_do_not_pass(self):
        for change in [dict(resident=1),dict(sky=15),dict(field_incarnation=0),dict(physical=0),
                       dict(coord='(1,-4,3)')]:
            with self.subTest(change=change):
                self.changes={25:change};self.check('FAIL')
        self.changes={25:dict(world_epoch=99)};self.check('UNTESTED')

    def test_invalid_initial_source_declaration_fails(self):
        self.extra=[[22,-49,51]];self.check('FAIL')

    def test_read_only_cold_probe_has_static_declared_source(self):
        self.extra=[[22,-49,51]]
        self.plan['entries'][0]=dict(cell=[22,-49,51],expected='glowstone')
        self.action['observations'][0].update(expected_id=376,before_id=376,api_called=False,api_success=False)
        self.action['observations'][1]['expected_id']=376
        self.material['cells'][0]['before_id']=376
        self.lights=[13]*40
        self.check('PASS')
