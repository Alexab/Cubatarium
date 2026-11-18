import json
from pathlib import Path
import tempfile
import unittest

from engine_fixture_reference import evaluate


class DepthAttributionTest(unittest.TestCase):
    def scene(self):
        return dict(schema="engine-smoke-fixture.v1", name="wet-negative-cave",
                    chunk_x_bounds=[-20, 3])

    def row(self, frame=0, distance=5.5):
        row = dict(kind="renderer_pixel_probe", frame_epoch=frame,
                   renderer_pixel_ray_origin_x=22, renderer_pixel_ray_origin_y=-36,
                   renderer_pixel_ray_origin_z=51, renderer_pixel_ray_direction_x=0,
                   renderer_pixel_ray_direction_y=1, renderer_pixel_ray_direction_z=0,
                   renderer_pixel_opaque_hit_distance=distance,
                   renderer_pixel_voxel_hit_distance=5.5)
        prefix = "renderer_pixel_voxel_chunk_"
        row.update({prefix + k: v for k, v in dict(x=1, y=-2, z=3,
                    published_geom_rev=0, dirty_queue_kind=1, dirty_queue_age_frames=130,
                    has_settled_light=1, settled_light_rev=1, field_light_rev=1,
                    work_owner_flags=1).items()})
        return row

    def check(self, rows):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "perf.jsonl"
            path.write_text("\n".join(json.dumps(row) for row in rows), encoding="utf-8")
            return evaluate(self.scene(), path)

    def test_missing_geometry_is_attributed_without_using_flags_as_verdict(self):
        row = self.row(frame=480, distance=30)
        result = self.check([row])
        self.assertEqual(result["status"], "FAIL")
        section = result["fault_attribution"]["sections"][0]
        self.assertEqual(section["coord"], [1, -2, 3])
        self.assertEqual(section["voxel_matches_reference"], 1)
        self.assertEqual(section["queued_without_published_geometry"], 1)
        self.assertEqual(section["reported_settled_revision"], 1)
        self.assertEqual(section["maximum_dirty_age"], 130)
        self.assertEqual(section["first_frame_epoch"], 480)
        self.assertEqual(section["work_owner_flag_counts"], {"1": 1})
        row["renderer_pixel_voxel_chunk_published_geom_rev"] = 99
        row["renderer_pixel_voxel_chunk_dirty_queue_kind"] = 0
        changed = self.check([row])
        self.assertEqual(changed["status"], "FAIL", "ready flags cannot hide missing depth")
        self.assertEqual(changed["faults"], 1)
        self.assertEqual(changed["fault_attribution"]["sections"][0]["no_published_geometry"], 0)

    def test_depth_passes_despite_diagnostic_unpublished_flags(self):
        result = self.check([self.row(frame=n // 40) for n in range(120)])
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(result["fault_attribution"]["sections"], [])

    def test_solid_side_attribution_uses_signed_floor_and_ceiling_cells(self):
        ceiling = self.row(frame=1, distance=-1)
        floor = self.row(frame=2, distance=-1)
        floor["renderer_pixel_ray_direction_y"] = -1
        floor["renderer_pixel_voxel_hit_distance"] = 7.5
        floor["renderer_pixel_voxel_chunk_y"] = -3
        result = self.check([ceiling, floor])
        self.assertEqual(result["faults"], 2)
        self.assertEqual({tuple(s["coord"]) for s in result["fault_attribution"]["sections"]},
                         {(1, -2, 3), (1, -3, 3)})

    def test_missing_or_unmatched_metadata_does_not_change_reference(self):
        for update in ({"renderer_pixel_voxel_chunk_x": 99},
                       {"renderer_pixel_voxel_chunk_x": "bad"},
                       {"renderer_pixel_voxel_hit_distance": None}):
            row = self.row(distance=-1)
            row.update(update)
            result = self.check([row])
            self.assertEqual(result["status"], "FAIL")
            section = result["fault_attribution"]["sections"][0]
            self.assertEqual(section["faults"], 1)
            self.assertEqual(section["voxel_matches_reference"], 0)
        legacy = {k: v for k, v in self.row(distance=-1).items()
                  if not k.startswith("renderer_pixel_voxel_")}
        self.assertEqual(self.check([legacy])["faults"], 1)


if __name__ == "__main__":
    unittest.main()
