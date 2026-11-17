import json
import tempfile
import unittest
from pathlib import Path

from engine_camera_light_reference import evaluate


class CameraLightReferenceTest(unittest.TestCase):
    def scene(self):
        return dict(name="open-sky-plane", chunk_x_bounds=[-32, 3],
                    plane_bounds=dict(floor=-60.5, z_low=31.5, z_high=79.5))

    def rows(self, change="", fixed=False):
        result = []
        for frame in range(30):
            x = 0 if fixed else -(frame % 10)*16
            result.append("[CameraLightSample] world_epoch=4 "
                f"frame_epoch={frame} cell=({x},-39,51) coord=({x//16},-3,3) "
                "physical=1 resident=0 ready=1 sky=15 block_light=0 field_incarnation=12 " + change + "\n")
        return result

    def check(self, rows, marker=True, preceding=True):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "runtime.log"
            payload = ""
            if marker:
                payload = (rows[0] if preceding else "") + "[FirstWorldFrameWitness] " + json.dumps(
                    dict(phase="first_world_paint", world_epoch=4)) + "\n"
                if preceding:
                    rows = rows[1:]
            path.write_text(payload + "".join(rows), encoding="utf-8")
            return evaluate(self.scene(), [path])

    def test_requires_physical_and_absent_geometry_coverage(self):
        self.assertEqual(self.check(self.rows())["status"], "PASS")
        self.assertEqual(self.check(self.rows(), False)["status"], "UNTESTED")
        self.assertEqual(self.check(self.rows(), preceding=False)["status"], "UNTESTED")
        self.assertEqual(self.check(self.rows(fixed=True))["status"], "UNTESTED")
        self.assertEqual(self.check(self.rows("resident=1"))["status"], "UNTESTED")
        self.assertEqual(self.check(self.rows("physical=0 ready=0 sky=0"))["status"], "UNTESTED")

    def test_rejects_missing_binding_bad_light_and_unready_first_frame(self):
        for change in ("ready=0", "sky=0", "block_light=1", "field_incarnation=0",
                       "coord=(1,-3,3)", "ready=bad"):
            with self.subTest(change=change):
                rows = self.rows()
                rows[0] = rows[0].rstrip() + " " + change + "\n"
                self.assertEqual(self.check(rows)["status"], "FAIL")


if __name__ == "__main__":
    unittest.main()
