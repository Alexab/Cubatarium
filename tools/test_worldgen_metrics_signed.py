"""Regression tests for worldgen diagnostics on signed world heights."""

from __future__ import annotations

import unittest

from worldgen_metrics_lib import (
    analyze_surface,
    cave_stats,
    cave_void_fluid_stats,
    compare_to_thresholds,
    count_shore_air_gaps,
    ocean_depth_stats,
    spawn_land_shape_metrics,
    terrain_surface_blocks,
    terrain_shape_stats,
    vegetation_block_category,
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

    def test_terrain_surface_ignores_canopy_blocks(self) -> None:
        columns = {
            (0, 4, 0): 1,
            (0, 5, 0): 2,
            (1, 4, 0): 1,
            (1, 5, 0): 2,
            (2, 4, 0): 1,
            (2, 5, 0): 2,
            (3, 4, 0): 1,
            (3, 5, 0): 2,
            (4, 4, 0): 1,
            (4, 5, 0): 2,
        }
        terrain = terrain_surface_blocks(columns, ignored_ids={2})
        result = analyze_surface(
            {(x, 0): (5, 2) for x in range(5)},
            terrain_surface=terrain,
        )

        self.assertEqual(result["height_min"], 4)
        self.assertEqual(result["height_max"], 4)
        self.assertEqual(result["flatness_pct"], 100.0)

    def test_spawn_shape_uses_spawn_center_and_dry_terrain_only(self) -> None:
        visible_surface = {
            (100, 200): (63, 9),
            (101, 200): (70, 1),
            (0, 0): (40, 2),
        }
        terrain_surface = {
            (100, 200): (12, 2),
            (101, 200): (68, 2),
            (0, 0): (40, 2),
        }

        result = spawn_land_shape_metrics(
            visible_surface,
            terrain_surface,
            center_x=100,
            center_z=200,
            radius=1,
            water_id=9,
        )

        self.assertEqual(result["spawn_center"], [100, 200])
        self.assertEqual(result["spawn_land_columns"], 1)
        self.assertEqual(result["spawn_land_height_mean"], 68)
        self.assertAlmostEqual(result["spawn_land_coverage_pct"], 100.0 / 9.0)
        self.assertEqual(result["spawn_flatness_pct"], 0.0)

    def test_terrain_shape_detects_broad_smooth_slope(self) -> None:
        surface = {
            (x, z): (x, 1)
            for x in range(-3, 4)
            for z in range(-3, 4)
        }

        result = terrain_shape_stats(surface)

        self.assertEqual(result["flatness_pct"], 0.0)
        self.assertEqual(result["rolling_hill_pct"], 100.0)

    def test_quality_gates_use_spawn_land_mean_and_normalized_tree_density(self) -> None:
        metrics = {
            "columns": 5000,
            "height_mean": 40.0,
            "spawn_land_height_mean": 70.0,
            "spawn_land_columns": 2000,
            "spawn_land_coverage_pct": 47.3,
            "spawn_tree_blocks": 800,
            "spawn_tree_blocks_land_normalized": 1691.0,
        }

        failures = compare_to_thresholds(
            metrics,
            {
                "height_mean_min": 63,
                "height_mean_max": 85,
                "spawn_meadow_min_pct": 0,
                "spawn_land_coverage_pct_min": 20,
                "spawn_tree_blocks_min": 1500,
            },
        )

        self.assertEqual(failures, [])

    def test_quality_gates_flag_spawn_with_too_little_land(self) -> None:
        failures = compare_to_thresholds(
            {
                "columns": 5000,
                "spawn_land_height_mean": 70,
                "spawn_land_coverage_pct": 0.14,
            },
            {
                "height_mean_max": 85,
                "spawn_meadow_min_pct": 0,
                "spawn_land_coverage_pct_min": 20,
            },
        )

        self.assertEqual(len(failures), 1)
        self.assertIn("spawn_land_coverage_pct=0.14%", failures[0])

    def test_terrain_quality_gate_uses_world_area_shape(self) -> None:
        failures = compare_to_thresholds(
            {
                "columns": 5000,
                "spawn_land_height_mean": 70.0,
                "flatness_pct": 40.0,
                "rolling_hill_pct": 12.0,
                "spawn_flatness_pct": 80.0,
                "spawn_rolling_hill_pct": 3.0,
            },
            {
                "height_mean_min": 63,
                "height_mean_max": 85,
                "spawn_meadow_min_pct": 0,
                "flatness_pct_max": 68,
                "rolling_hill_pct_min": 5,
            },
        )

        self.assertEqual(failures, [])

    def test_surface_spawn_slots_use_saved_spawn_center(self) -> None:
        result = analyze_surface(
            {(100, 200): (65, 1), (0, 0): (70, 2)},
            spawn_radius=0,
            center_x=100,
            center_z=200,
            id_to_name={1: "grass", 2: "stone"},
            slot_map={"grass": ["grass"], "stone": ["stone"]},
        )

        self.assertEqual(result["spawn"]["columns"], 1)
        self.assertEqual(result["spawn"]["surface_slots"], {"grass": 100.0})

    def test_vegetation_taxonomy_includes_profile_two_species(self) -> None:
        self.assertEqual(vegetation_block_category("mtg_aspen_tree"), "tree")
        self.assertEqual(vegetation_block_category("mtg_acacia_leaves"), "tree")
        self.assertEqual(vegetation_block_category("mtg_jungletree"), "tree")
        self.assertEqual(vegetation_block_category("mtg_bush_stem"), "shrub")
        self.assertEqual(
            vegetation_block_category("mtg_blueberry_bush_leaves_with_berries"),
            "shrub",
        )
        self.assertIsNone(vegetation_block_category("mtg_aspen_sapling"))


if __name__ == "__main__":
    unittest.main()
