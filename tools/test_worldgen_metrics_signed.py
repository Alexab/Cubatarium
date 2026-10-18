"""Regression tests for worldgen diagnostics on signed world heights."""

from __future__ import annotations

import unittest

from worldgen_metrics_lib import (
    analyze_surface,
    cave_stats,
    cave_void_fluid_stats,
    count_shore_air_gaps,
    ocean_depth_stats,
    world_vertical_bounds,
)


class SignedWorldMetricTests(unittest.TestCase):
    def test_profile_bounds_are_read_and_legacy_saves_stay_unsigned(self) -> None:
        self.assertEqual(
            world_vertical_bounds(
                {"procedural": {"min_height": -64, "max_height": 319}}
            ),
            (-64, 319),
        )
        self.assertEqual(world_vertical_bounds({}), (0, 319))

    def test_ocean_depth_walks_below_zero_to_profile_minimum(self) -> None:
        columns = {
            (0, 2, 0): 2,
            (0, 1, 0): 2,
            (0, 0, 0): 2,
            (0, -1, 0): 2,
            (0, -2, 0): 1,
        }

        result = ocean_depth_stats(
            columns,
            sea_level=2,
            water_id=2,
            min_world_y=-4,
        )

        self.assertEqual(result["ocean_columns"], 1)
        self.assertEqual(result["ocean_depth_max"], 4)

    def test_cave_air_volume_includes_negative_y(self) -> None:
        result = cave_stats(
            {
                (0, -2, 0): 1,
                (0, -1, 0): 0,
                (0, 0, 0): 1,
                (0, 1, 0): 0,
            },
            {(0, 0): 2},
            min_world_y=-2,
        )

        self.assertEqual(result["cave_air_volume"], 2)
        self.assertEqual(result["max_cave_depth_below_surface"], 3)

    def test_cave_fluid_counts_include_negative_y(self) -> None:
        columns = {
            (0, -2, 0): 1,
            (0, -1, 0): 0,
            (0, 0, 0): 0,
            (0, 1, 0): 2,
        }

        result = cave_void_fluid_stats(
            columns,
            {(0, 0): 2},
            {0: "air", 1: "stone", 2: "water"},
            min_world_y=-2,
        )

        self.assertEqual(result["cave_dry_air_voxels"], 2)
        self.assertEqual(result["cave_water_voxels"], 1)
        self.assertEqual(result["cave_void_voxels"], 3)

    def test_shore_gap_scan_includes_negative_y(self) -> None:
        columns = {
            (0, -2, 0): 0,
            (1, -2, 0): 2,
        }

        self.assertEqual(
            count_shore_air_gaps(
                columns,
                water_id=2,
                sea_level=0,
                min_world_y=-4,
            ),
            1,
        )

    def test_surface_reports_profile_bounds_and_violations(self) -> None:
        result = analyze_surface(
            {(0, 0): (-65, 1), (1, 0): (319, 1)},
            min_height=-64,
            max_height=319,
        )

        self.assertEqual(result["world_min_height"], -64)
        self.assertEqual(result["world_max_height"], 319)
        self.assertEqual(result["min_height_violations"], 1)
        self.assertEqual(result["max_height_violations"], 0)


if __name__ == "__main__":
    unittest.main()
