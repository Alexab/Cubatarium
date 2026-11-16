"""Regression: a correctly oriented loose copy must not hide a bad held item."""
import copy
import unittest

from audit_item_hand_alignment import inspect_world, inspect_directions
from itemgen.presentation import prepare_presentation


class HandAlignmentTests(unittest.TestCase):
    def test_loose_copy_cannot_satisfy_held_direction(self):
        identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
        forward = [0, 0, -1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 10, 0, 0, 1]
        contracts = {'axe': {'checks': [dict(axis=[1, 0, 0], direction='forward',
                                            minimum=.7, absolute=False)]}}
        draw = dict(path='models/items/axe/model.gltf', right_grip=[0, 0, 0],
                    left_grip=[0, 0, 0], visible_primitives=1)
        frame = dict(id='axe', active_item='axe', matching_body=True,
                     camera_front=[0, 0, -1], has_wield_palm=True, wield_palm=[0, 0, 0],
                     world_items=[dict(draw, model_world=identity),
                                  dict(draw, model_world=forward)])
        report = dict(passed=True, request=dict(perspective='back', body='male'), frames=[frame])
        self.assertFalse(inspect_world(report, contracts)[0]['passed'])
        repaired = copy.deepcopy(report)
        repaired['frames'][0]['world_items'][0]['model_world'] = forward[:12]+identity[12:]
        self.assertTrue(inspect_world(repaired, contracts)[0]['passed'])
        missing = copy.deepcopy(report)
        missing['frames'][0]['world_items'].pop(0)
        with self.assertRaisesRegex(ValueError, 'palm-attached'):
            inspect_world(missing, contracts)

    def test_direction_is_signed_and_scale_independent(self):
        contract = {'checks': [dict(axis=[1, 0, 0], direction='forward', minimum=.7, absolute=False)]}
        matrix = [0, 0, -3, 0, 0, 3, 0, 0, 3, 0, 0, 0, 0, 0, 0, 1]
        self.assertTrue(inspect_directions(matrix, [0, 0, -1], contract)[0]['passed'])
        self.assertFalse(inspect_directions(matrix, [0, 0, 1], contract)[0]['passed'])

    def test_new_rotations_remain_angular_not_scaled_coordinates(self):
        source = dict(tp_left_euler_deg=[-65, 90, 0], fp_use_euler_deg=[76, 0, -20],
                      fp_use_hand_offset=[0, .4, 0])
        self.assertEqual(prepare_presentation(source, 2, [1, 2, 3]), source)
        with self.assertRaises(ValueError):
            prepare_presentation(dict(tp_left_euler_deg=[1, 2]), 2, [0, 0, 0])


if __name__ == '__main__':
    unittest.main()
