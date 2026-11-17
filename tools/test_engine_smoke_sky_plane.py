import json
import tempfile
import unittest
from pathlib import Path

from engine_fixture_reference import near_cave_surface, evaluate as depth_reference
from engine_floor_light_reference import evaluate


class SkyPlaneReferenceTest(unittest.TestCase):
    def scene(self):
        return dict(name="open-sky-plane", schema="engine-smoke-fixture.v1",
                    chunk_x_bounds=[-5, 3], plane_bounds=dict(floor=-60.5, z_low=31.5, z_high=79.5),
                    lighting_fixture=dict(schema="engine-direct-skylight-fixture.v1",
                                          sky_level=15, material_id=560))

    def test_open_floor_has_no_analytic_ceiling_or_walls(self):
        scene = self.scene()
        self.assertEqual(near_cave_surface(scene, [0, -39, 51], [0, -1, 0]), 21.5)
        for origin, direction in [([0, -39, 51], [0, 1, 0]),
                                  ([0, -39, 51], [0, 0, 1]),
                                  ([0, -39, 90], [0, -1, 0]),
                                  ([0, 0, 51], [0, -1, 0])]:
            self.assertIsNone(near_cave_surface(scene, origin, direction))

    def test_direct_sky_requires_readable_nonzero_source_and_spatial_coverage(self):
        p = "renderer_pixel_opaque_"
        row = dict(kind="renderer_pixel_probe", renderer_pixel_ray_origin_y=-39,
                   renderer_pixel_ray_origin_z=51, renderer_pixel_ray_direction_x=0,
                   renderer_pixel_ray_direction_y=-1, renderer_pixel_ray_direction_z=0)
        row.update({p+"hit_distance": 21.5, p+"surface_valid": 1,
                    p+"vertex_light_valid": 1, p+"vertex_light_face_index": 4,
                    p+"drawable": 1, p+"draw_ready": 1, p+"vertex_light_distance": 0,
                    p+"vertex_light_preview": 0, p+"preview_shader_override": 0,
                    p+"light_source_kind": 2, p+"packed_light_payload_valid": 1,
                    p+"packed_light_quad_count": 1, p+"vertex_block_light": 0,
                    p+"vertex_sky_light": 1, p+"vertex_light_block_id": 560})
        with tempfile.TemporaryDirectory() as temp:
            trace = Path(temp) / "trace.jsonl"
            def check(change=None):
                rows = [dict(row, renderer_pixel_ray_origin_x=i % 10, frame_epoch=i//10)
                        for i in range(30)]
                for sample in rows:
                    sample.update(change or {})
                trace.write_text("".join(json.dumps(r)+"\n" for r in rows))
                return evaluate(self.scene(), trace)
            self.assertEqual(check()["status"], "PASS")
            self.assertEqual(check({p+"vertex_sky_light": 0})["status"], "FAIL")
            self.assertEqual(check({p+"vertex_block_light": 1/15})["status"], "FAIL")
            self.assertEqual(check({p+"vertex_light_block_id": 376})["status"], "FAIL")
            self.assertEqual(check({p+"packed_light_payload_valid": 0})["status"], "UNTESTED")
            self.assertEqual(check({p+"vertex_light_preview": 1})["status"], "UNTESTED")
            self.assertEqual(check({"renderer_pixel_ray_origin_x": 0})["status"], "UNTESTED")
            check({p+"hit_distance": -1})
            self.assertEqual(depth_reference(self.scene(), trace)["status"], "FAIL")


if __name__ == "__main__":
    unittest.main()
