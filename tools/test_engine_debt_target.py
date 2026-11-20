import copy
import json
from pathlib import Path
import tempfile
import unittest

from engine_debt_target import select, author, digest


class DebtTargetTest(unittest.TestCase):
    def test_selection_requires_actual_failed_cell_and_unchanged_origin(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            scene = dict(name="open-sky-plane", chunk_x_bounds=[-12, 3],
                         starting_eye=[22, -58.88, 51],
                         plane_bounds=dict(floor=-60.5, z_low=31.5, z_high=79.5),
                         lighting_fixture=dict(material_id=560))
            fault = dict(status="FAIL", examples=[dict(floor_cell=[-124, -61, 51],
                expected_sky_level=15, source_sky_light=0, source_material_id=560)])
            verdict = dict(components=dict(fixture_floor_draw_source_light=fault))
            (root/"fixture.json").write_text(json.dumps(scene))
            (root/"route.json").write_text(json.dumps(dict(source_world_sha256="a"*64)))
            def write():
                (root/"smoke-verdict.json").write_text(json.dumps(verdict))
            write()
            selection = select(root)
            self.assertEqual(selection["cell"], [-124, -61, 51])
            route = dict(relative=True, movement_mode="walk", waypoints=[
                dict(position=[0, 0, 0]), dict(position=[-176, 0, 0])])
            def build(**kwargs):
                options = dict(fixture_hash=digest(root/"fixture.json"),
                               source_hash="a"*64, minimum_chunks=8)
                options.update(kwargs)
                return author(copy.deepcopy(route), selection, scene, **options)
            target = build()["waypoints"][1]
            self.assertEqual(target["position"], [-146, 0, 0])
            self.assertAlmostEqual(target["inspection_targets"][0][1], -1.62)
            for kwargs in [dict(fixture_hash="b"*64), dict(source_hash="b"*64), dict(minimum_chunks=16)]:
                with self.assertRaises(ValueError): build(**kwargs)
            fault["examples"][0]["source_sky_light"] = 1
            write()
            with self.assertRaises(ValueError): build()
            with self.assertRaises(ValueError): select(root)
            fault["examples"][0]["source_sky_light"] = 0
            fault["status"] = "PASS"
            write()
            with self.assertRaises(ValueError): select(root)
            fault["status"] = "FAIL"
            for cell in ([1000, -61, 51], [-124, -60, 51], [-124, -61, float("nan")]):
                fault["examples"][0]["floor_cell"] = cell
                write()
                with self.assertRaises(ValueError): select(root)


if __name__ == "__main__":
    unittest.main()
