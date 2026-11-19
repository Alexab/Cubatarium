import copy
import json
from pathlib import Path
import struct
import tempfile
import unittest

from engine_world_actions import evaluate, read_binary_cell, read_disk_cell, validate_plan


def binary(version=4, *, material=376, coord=(1,-3,3)):
    return (struct.pack('<4sBiiiHHIHH', b'CCHK', version, *coord, 1, material, 1, 4096, 0) +
            (struct.pack('<I',4096) + bytes(4096) if version >= 2 else b'') +
            (struct.pack('<I',4096) + bytes(4096) if version >= 3 else b'') +
            (struct.pack('<HQ',0,0) if version >= 4 else b''))


class WorldActionsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.world = self.root/'EngineSmoke_test'
        (self.world/'chunks').mkdir(parents=True)
        self.cell = [24,-44,51]
        self.plan = dict(schema='engine-native-world-actions.v1', entries=[
            dict(cell=self.cell, expected='stone', replacement='glowstone')])
        self.write_route()
        self.observation = dict(schema='engine-world-actions-observation.v1', world=self.world.name,
                                api='UWorld::ReplaceBlockAt', started=True, failed=False,
                                saved_world_label='World_261',world_folder=str(self.world.resolve()),
                                storage_matches_isolated_world=True,
                                observations=[dict(phase='initial',cell=self.cell,section=[1,-3,3],
                                    expected_id=560,target_id=376,before_id=560,after_id=376,
                                    resident=True,incarnation=7,reach_blocks=3,api_called=True,
                                    api_success=True,success=True,steady_tick_ms=10),
                                    dict(phase='final',cell=self.cell,section=[1,-3,3],
                                    expected_id=560,target_id=376,before_id=376,after_id=376,
                                    resident=True,incarnation=19,api_called=False,api_success=False,
                                    success=True,steady_tick_ms=50)])
        self.route = dict(status='PASS',sections=[dict(coord=[1,-3,3],retired_incarnation=7,
                            returned_incarnation=19)],far_tick_ms=20,return_tick_ms=40)
        self.path = self.world/'chunks/1_-3_3.cchunk'
        self.path.write_bytes(binary())

    def write_route(self):
        (self.root/'bot-route.json').write_text(json.dumps(dict(
            schema='diagnostic-flight-route.v1',purpose='round_trip',
            isolated_world=self.world.name,world_actions=self.plan)),encoding='utf-8')

    def run_evidence(self):
        (self.root/'world-actions.json').write_text(json.dumps(self.observation),encoding='utf-8')
        return evaluate(self.root,self.world,self.plan,self.route)

    def test_edit_return_and_independent_disk(self):
        self.assertEqual(self.run_evidence()['status'],'PASS')
        self.path.write_bytes(binary(material=560))
        self.assertEqual(self.run_evidence()['reason'],'persisted_material_mismatch')

    def test_forged_api_or_held_section_cannot_pass(self):
        for field, value in [('api_called',False),('api_success',False),('reach_blocks',7),
                             ('before_id',376),('incarnation',19),('resident',False)]:
            with self.subTest(field=field):
                original=copy.deepcopy(self.observation)
                self.observation['observations'][0][field]=value
                self.assertEqual(self.run_evidence()['status'],'FAIL')
                self.observation=original

    def test_missing_route_or_native_cannot_pass(self):
        self.assertEqual(evaluate(self.root,self.world,self.plan,self.route)['status'],'UNTESTED')
        self.route['status']='UNTESTED'
        self.assertEqual(self.run_evidence()['status'],'UNTESTED')

    def test_wrong_world_duplicate_row_and_mutation_at_final(self):
        self.observation['world']='World_261'
        self.assertEqual(self.run_evidence()['status'],'FAIL')
        self.observation['world']=self.world.name
        self.observation['observations'][1]['api_called']=True
        self.assertEqual(self.run_evidence()['status'],'FAIL')
        self.observation['observations'][1]=copy.deepcopy(self.observation['observations'][0])
        self.assertEqual(self.run_evidence()['status'],'FAIL')

    def test_read_only_cold_probe(self):
        self.plan['entries'][0] = dict(cell=self.cell, expected='glowstone')
        self.write_route()
        for row in self.observation['observations']:
            row.update(expected_id=376,before_id=376,api_called=False,api_success=False)
        self.route=None
        self.assertEqual(self.run_evidence()['status'],'PASS')

    def test_expected_names_must_match_the_executed_plan(self):
        self.assertEqual(self.run_evidence()['status'],'PASS')
        self.plan['entries'][0]['expected']='dirt'
        self.assertEqual(self.run_evidence()['reason'],'native_action_plan_mismatch')

    def test_saved_label_is_not_storage_identity(self):
        self.assertEqual(self.run_evidence()['status'],'PASS')
        self.observation['world_folder']=str(self.root/'World_261')
        self.assertEqual(self.run_evidence()['status'],'FAIL')
        self.observation['world_folder']=str(self.world)
        self.observation['storage_matches_isolated_world']=False
        self.assertEqual(self.run_evidence()['status'],'FAIL')

    def test_plan_bounds(self):
        validate_plan(self.plan,round_trip=True)
        with self.assertRaises(ValueError): validate_plan(self.plan)
        for value in [True,1.0,2**31,-2**31-1]:
            plan=copy.deepcopy(self.plan); plan['entries'][0]['cell']=[value,0,0]
            with self.assertRaises(ValueError): validate_plan(plan,round_trip=True)
        plan=copy.deepcopy(self.plan); plan['entries']*=2
        with self.assertRaises(ValueError): validate_plan(plan,round_trip=True)

    def test_all_binary_versions_and_bounds(self):
        for version in (1,2,3,4):
            data=binary(version)
            self.assertEqual(read_binary_cell(data,(1,-3,3),(8,4,3)),376)
            for corrupt in (data[:-1],data+b'X',binary(version,coord=(0,0,0))):
                with self.assertRaises(ValueError): read_binary_cell(corrupt,(1,-3,3),(8,4,3))

    def test_binary_x_fast_y_then_z(self):
        index=8+4*16+3*256
        data=(struct.pack('<4sBiiiHHHI',b'CCHK',1,1,-3,3,2,560,376,3)+
              struct.pack('<HHHHHH',index,0,1,1,4095-index,0))
        self.assertEqual(read_binary_cell(data,(1,-3,3),(8,4,3)),376)
        self.assertEqual(read_binary_cell(data,(1,-3,3),(9,4,3)),560)

    def test_malformed_runs_and_tails(self):
        data=binary(1)
        for start,replacement in [(25,struct.pack('<H',0)),(27,struct.pack('<H',1)),
                                   (25,struct.pack('<H',4095)),(21,struct.pack('<I',4097))]:
            corrupt=data[:start]+replacement+data[start+len(replacement):]
            with self.assertRaises(ValueError): read_binary_cell(corrupt,(1,-3,3),(8,4,3))

    def test_json_id_precedence_and_duplicate_rejection(self):
        self.path.unlink()
        path=self.path.with_suffix('.json')
        doc=dict(cx=1,cy=-3,cz=3,voxels=[dict(lx=8,ly=4,lz=3,id=376,type='stone')])
        path.write_text(json.dumps(doc),encoding='utf-8')
        self.assertEqual(read_disk_cell(self.world,self.cell)['id'],376)
        self.assertEqual(self.run_evidence()['status'],'PASS')
        doc['voxels']*=2; path.write_text(json.dumps(doc),encoding='utf-8')
        self.assertEqual(self.run_evidence()['status'],'FAIL')


if __name__ == '__main__':
    unittest.main()
