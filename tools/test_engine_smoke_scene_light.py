import json
import tempfile
import unittest
from pathlib import Path

from engine_scene_light_reference import evaluate


class SceneLightReferenceTest(unittest.TestCase):
    def check(self, *, change="", omit=-1, consumer="fp_item", duplicate=False,
              expected_frames=30, camera_bad=False):
        scene = dict(name="open-sky-plane", chunk_x_bounds=[-32, 3],
                     plane_bounds=dict(floor=-60.5, z_low=31.5, z_high=79.5))
        rows = []
        for frame in range(30):
            x = -(frame % 10)*16
            fields = (f"world_epoch=4 frame_epoch={frame} cell=({x},-39,51) "
                      f"coord=({x//16},-3,3) physical=1 resident=0 ready=1 "
                      "sky=15 block_light=0 field_incarnation=12")
            rows.append("[CameraLightSample] " + fields +
                        (" ready=0" if camera_bad and frame == 0 else "") + "\n")
            if frame == 0:
                rows.append("[FirstWorldFrameWitness] " + json.dumps(
                    dict(phase="first_world_paint", world_epoch=4)) + "\n")
            if frame != omit:
                row = (f"[SceneLightSample] consumer={consumer} " + fields +
                       " state=2 field_revision=0 " + (change if frame == 0 else "") + "\n")
                rows.append(row)
                if duplicate and frame == 1:
                    rows.append(row)
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "native.log"
            path.write_text("".join(rows), encoding="utf-8")
            return evaluate(scene, [path], expected_frames=expected_frames)

    def test_requires_every_actual_first_person_frame(self):
        result = self.check()
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(result["ready_frames"], 30)
        for options in (dict(omit=0), dict(omit=15), dict(consumer="creature"),
                        dict(consumer="item"), dict(expected_frames=31),
                        dict(camera_bad=True)):
            with self.subTest(options=options):
                self.assertEqual(self.check(**options)["status"], "UNTESTED")

    def test_rejects_stale_missing_constant_or_wrong_field(self):
        for change in ("ready=0", "state=1", "state=0", "state=3", "state=4",
                       "field_incarnation=0", "sky=0", "block_light=1",
                       "field_revision=-1", "coord=(1,-3,3)", "ready=bad",
                       "frame_epoch=99"):
            with self.subTest(change=change):
                self.assertEqual(self.check(change=change)["status"], "FAIL")
        self.assertEqual(self.check(duplicate=True)["status"], "FAIL")
        self.assertEqual(self.check(change="physical=0")["status"], "UNTESTED")


if __name__ == "__main__":
    unittest.main()
