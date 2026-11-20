import copy
import math
import unittest

from engine_source_hole_reference import HoleReference


def scene():
    return dict(schema='engine-smoke-fixture.v1', name='dry-negative-cave',
        expected_eye_in_fluid=False, chunk_x_bounds=[-4, 3],
        cave_bounds=dict(floor=-48.5, ceiling=-16.5, z_low=34.5, z_high=67.5),
        lighting_fixture=dict(schema='engine-positive-blocklight-fixture.v1',
                              material_id=376, emission=15),
        air_border_fixture=dict(schema='engine-positive-air-border-fixture.v1'))


def ray(origin, target):
    delta = [b-a for a, b in zip(origin, target)]
    length = math.sqrt(sum(v*v for v in delta))
    return [v/length for v in delta]


class SourceHoleReferenceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.emitters = [[x, -49, 51] for x in range(-64, 64, 8)]
        cls.before = HoleReference(scene(), cls.emitters+[[22, -49, 51]])
        cls.after = HoleReference(scene(), cls.emitters, [22, -49, 51])

    def test_bfs_agrees_with_independent_upper_air_shortest_path(self):
        # An independent closed form is valid above the intact flat floor.
        # Cross signed section boundaries and include zero-light cells.
        for x in range(-48, 48):
            for y in (-48, -47, -40, -34, -17):
                for z in (35, 47, 51, 63, 67):
                    expected = max(0, 15-min(abs(x-e[0])+abs(y-e[1])+abs(z-e[2])
                                             for e in self.before.emitters))
                    self.assertEqual(self.before.block_level([x, y, z]), expected)

    def test_removed_source_requires_detour_around_opaque_neighbor(self):
        self.assertEqual(self.before.block_level([22, -49, 51]), 15)
        self.assertEqual(self.before.block_level([22, -48, 51]), 14)
        self.assertEqual(self.after.block_level([22, -48, 51]), 12)
        self.assertEqual(self.after.block_level([22, -49, 51]), 11)
        naive = 15-min(abs(22-e[0]) for e in self.emitters)
        self.assertEqual(naive, 13)
        self.assertNotEqual(self.after.block_level([22, -49, 51]), naive)
        with self.assertRaises(ValueError):
            self.after.block_level([23, -49, 51])
        with self.assertRaises(ValueError):
            self.after.block_level([22, -50, 51])

    def test_hole_has_bottom_and_all_four_walls_with_correct_air_sample(self):
        origin = [22, -45.5, 51]
        for target, solid, face in (
                ([22, -49.5, 51], (22, -50, 51), 4),
                ([22.5, -49.1, 51], (23, -49, 51), 3),
                ([21.5, -49.1, 51], (21, -49, 51), 1),
                ([22, -49.1, 51.5], (22, -49, 52), 2),
                ([22, -49.1, 50.5], (22, -49, 50), 0)):
            with self.subTest(face=face):
                direction = ray(origin, target)
                surface = self.after.surface(origin, direction)
                self.assertIsNotNone(surface)
                self.assertAlmostEqual(surface.distance, math.dist(origin, target))
                self.assertEqual((surface.solid, surface.face), (solid, face))
                self.assertEqual(surface.air, (22, -49, 51))
                self.assertEqual(surface.material, 560)
                self.assertTrue(surface.hole)
                # A stale former glowstone top is a distinct geometry/depth.
                stale = self.before.surface(origin, direction)
                self.assertIsNotNone(stale)
                self.assertEqual(stale.material, 376)
                self.assertLess(stale.distance, surface.distance-.1)

    def test_ordinary_floor_is_preserved_but_source_material_changes(self):
        for x, material in ((20, 560), (24, 376)):
            surface = self.after.surface([x, -45.5, 51], [0, -1, 0])
            self.assertFalse(surface.hole)
            self.assertEqual(surface.solid, (x, -49, 51))
            self.assertEqual(surface.material, material)
            self.assertEqual(surface.face, 4)
            self.assertEqual(surface.distance, 3)

    def test_negative_coordinate_hole_and_signed_floor(self):
        model = HoleReference(scene(), self.emitters, [-2, -49, 51])
        surface = model.surface([-2, -45.5, 51], [0, -1, 0])
        self.assertEqual(surface.solid, (-2, -50, 51))
        self.assertEqual(model.block_level(surface.air), 11)

    def test_edges_unknown_extents_and_invalid_rays_cannot_supply_reference(self):
        for origin, direction in (([22, -49, 51], [0, -1, 0]),
                ([22, -45.5, 51], [0, 1, 0]), ([22, -45.5, 51], [0, -2, 0]),
                ([22, -45.5, 51], [float('nan'), -1, 0]),
                ([-63, -45.5, 51], [0, -1, 0]),
                ([23.49, -45.5, 51], [0, -1, 0])):
            self.assertIsNone(self.after.surface(origin, direction))
        with self.assertRaises(ValueError):
            self.after.block_level([64, -48, 51])

    def test_unsupported_contract_is_rejected_without_observational_truth(self):
        for key, value in (('name', 'wet-negative-cave'), ('expected_eye_in_fluid', True),
                           ('chunk_x_bounds', [-4, True])):
            altered = copy.deepcopy(scene())
            altered[key] = value
            with self.assertRaises(ValueError):
                HoleReference(altered, self.emitters, [22, -49, 51])
        for emitters, hole in ((self.emitters+[[22, -49, 51]], [22, -49, 51]),
                (self.emitters+[self.emitters[0]], None),
                (self.emitters, [22, -48, 51]), (self.emitters, [63, -49, 51])):
            with self.assertRaises(ValueError):
                HoleReference(scene(), emitters, hole)


if __name__ == '__main__':
    unittest.main()
