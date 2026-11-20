import copy
import unittest
from engine_source_observation import build_route


class SourceObservationTest(unittest.TestCase):
    def setUp(self):
        self.route=dict(purpose='round_trip',relative=True,waypoints=[
            dict(position=[0,6,0]),dict(position=[-304,6,0]),dict(position=[0,6,0])])
        self.scene=dict(air_border_fixture=dict(schema='engine-positive-air-border-fixture.v1'),
            lighting_fixture=dict(schema='engine-positive-blocklight-fixture.v1',material_id=376,emission=15,
                emitters=[[x,-49,51] for x in range(-320,49,8)]),rgb_fixture=dict(schema='engine-constant-floor-rgb-fixture.v1'),
            expected_eye_in_fluid=False,cave_bounds=dict(floor=-48.5),starting_eye=[22,-46.88,51])
        self.plan=dict(after_waypoints=1,entries=[dict(cell=[22,-49,51],expected='stone',replacement='glowstone')])

    def test_inspection_precedes_edit_and_retains_three_motor_waypoints(self):
        original=copy.deepcopy(self.route)
        r=build_route(self.route,self.scene,self.plan,[22,-46.88,51])
        self.assertEqual(self.route,original)
        self.assertEqual(len(r['waypoints']),3)
        self.assertEqual(r['waypoints'][0]['position'],[0,0,0])
        self.assertEqual(r['waypoints'][0]['position'],r['waypoints'][2]['position'])
        self.assertEqual(r['waypoints'][1]['position'],[-304,6,0])
        for point in r['waypoints']:
            self.assertTrue(point['inspect'])
            self.assertEqual(len(point['inspection_targets']),8)
        target=r['waypoints'][0]['inspection_targets'][0]
        self.assertEqual([target[0],target[2]],[2,0])
        self.assertAlmostEqual(target[1],-1.62)

    def test_no_implicit_source_mutation_timing_or_saved_pose_change(self):
        for gate in (None,0,True,2):
            self.plan['after_waypoints']=gate
            with self.assertRaises(ValueError):build_route(self.route,self.scene,self.plan,[22,-46.88,51])
        self.plan['after_waypoints']=1
        with self.assertRaises(ValueError):build_route(self.route,self.scene,self.plan,[22,68,51])
        self.plan['entries'][0]['cell']=[100,-49,51]
        with self.assertRaises(ValueError):build_route(self.route,self.scene,self.plan,[22,-46.88,51])

    def test_air_hole_and_non_round_trip_need_other_observation_contracts(self):
        self.plan['entries'][0]['replacement']='air'
        with self.assertRaises(ValueError):build_route(self.route,self.scene,self.plan,[22,-46.88,51])
        self.plan['entries'][0]['replacement']='glowstone';self.route['waypoints'].pop()
        with self.assertRaises(ValueError):build_route(self.route,self.scene,self.plan,[22,-46.88,51])
