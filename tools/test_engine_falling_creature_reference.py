import copy
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from engine_falling_creature_reference import evaluate
from engine_smoke_fixtures import prepare_falling_creature_plane

class FallingCreatureReferenceTest(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.log = self.root / 'sample.log'
        self.scene = {'chunk_x_bounds': [-23, 3], 'plane_bounds': {'z_low': 31.5, 'z_high': 79.5}, 'falling_creature': {'identity': 4, 'minimum_samples': 3, 'minimum_sections': 2, 'minimum_vertical_span': 8, 'minimum_fall_speed': 30, 'min_probe_y': -56.5, 'max_probe_y': 19}}
        self.lines = ['[FirstWorldFrameWitness] {"phase":"first_world_paint","world_epoch":4}']
        for frame, y in [(10, -36), (11, -44), (12, -52)]:
            self.lines.append(f'[CameraLightSample] world_epoch=4 frame_epoch={frame} coord=(3,-3,3)')
            self.lines.append(f'[SceneLightSample] world_epoch=4 frame_epoch={frame} consumer=creature consumer_identity=4 velocity=(0,-45,0) cell=(24,{y},51) coord=(1,{y // 16},3) physical=1 ready=1 state=2 sky=15 block_light=0 field_incarnation=42')

    def run_case(self, lines=None, scene=None):
        self.log.write_text('\n'.join(self.lines if lines is None else lines) + '\n', encoding='utf-8')
        return evaluate(self.scene if scene is None else scene, [self.log])

    def test_real_fall_scope_passes(self):
        result = self.run_case()
        self.assertEqual(result['status'], 'PASS')
        self.assertEqual(result['samples'], 3)

    def test_unready_target_fails(self):
        result = self.run_case([s.replace('ready=1 state=2', 'ready=0 state=0') for s in self.lines])
        self.assertEqual(result['status'], 'FAIL')

    def test_wrong_actor_cannot_supply_coverage(self):
        self.assertEqual(self.run_case([s.replace('consumer_identity=4', 'consumer_identity=5') for s in self.lines])['status'], 'UNTESTED')

    def test_camera_prefetch_cannot_supply_independent_coverage(self):
        lines = [s.replace('coord=(3,-3,3)', 'coord=(1,-3,3)') if 'Camera' in s else s for s in self.lines]
        self.assertEqual(self.run_case(lines)['status'], 'UNTESTED')

    def test_stationary_actor_cannot_supply_fall_coverage(self):
        self.assertEqual(self.run_case([s.replace('(0,-45,0)', '(0,0,0)') for s in self.lines])['status'], 'UNTESTED')

    def test_missing_identity_cannot_pass(self):
        self.assertEqual(self.run_case([s.replace('consumer_identity=4 ', '') for s in self.lines])['status'], 'UNTESTED')

    def test_missing_paint_fails(self):
        self.assertEqual(self.run_case([s for s in self.lines if not ('Camera' in s and 'epoch=11' in s)])['status'], 'FAIL')

    def test_duplicate_samples_do_not_invent_coverage(self):
        self.assertEqual(self.run_case([self.lines[0], self.lines[1], self.lines[2], self.lines[2], self.lines[2]])['status'], 'UNTESTED')

    def test_outside_authored_geometry_cannot_supply_coverage(self):
        self.assertEqual(self.run_case([s.replace('24,', '1024,').replace('coord=(1,', 'coord=(64,') for s in self.lines])['status'], 'UNTESTED')

    def test_authored_actor_preserves_other_saved_rows(self):
        fixture = self.root / 'worlds' / 'EngineSmoke_test'
        fixture.mkdir(parents=True)
        actors = {'creatures': [{'id': 1, 'type': 'human', 'body_origin': [1, 2, 3]}, {'id': 4, 'type': 'tortoise', 'body_origin': [4, 5, 6]}, {'id': 5, 'type': 'tortoise', 'body_origin': [7, 8, 9]}]}
        (fixture / 'creatures.json').write_text(json.dumps(actors))
        (fixture / 'users.json').write_text(json.dumps({'Username': {'player_creature_id': 1}}))
        base = {'starting_eye': [22, -58.88, 51], 'plane_bounds': {'floor': -60.5}, 'expected_max_eye_y': 19}
        with patch('engine_smoke_fixtures.prepare_visible_items_plane', return_value=base):
            scene = prepare_falling_creature_plane(fixture, 19)
        changed = json.loads((fixture / 'creatures.json').read_text())
        self.assertEqual(changed['creatures'][0], actors['creatures'][0])
        self.assertEqual(changed['creatures'][2], actors['creatures'][2])
        self.assertEqual(changed['creatures'][1]['body_origin'], [-3, 63.5, 51])
        self.assertEqual(scene['falling_creature']['identity'], 4)
if __name__ == '__main__':
    unittest.main()
