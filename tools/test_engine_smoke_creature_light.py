import json
import tempfile
import unittest
from pathlib import Path

from engine_creature_light_reference import evaluate


class CreatureLightReferenceTest(unittest.TestCase):
    def check(self, change="", *, omit=-1, marker=True, consumer="creature", z=39):
        scene = dict(name="open-sky-plane", chunk_x_bounds=[-23, 3],
                     plane_bounds=dict(floor=-60.5, z_low=31.5, z_high=79.5),
                     expected_max_eye_y=19)
        rows = []
        for frame, y in enumerate((-20, -38, -48)):
            fields = (f"world_epoch=4 frame_epoch={frame} cell=(-3,{y},{z}) "
                      f"coord=(-1,{y//16},{z//16}) physical=1 resident=0 ready=1 "
                      "state=2 sky=15 block_light=0 field_incarnation=12 field_revision=0")
            if omit != frame:
                rows.append("[CameraLightSample] " + fields + "\n")
            rows.append(f"[SceneLightSample] consumer={consumer} " + fields +
                        (" " + change if frame == 1 else "") + "\n")
            if frame == 0 and marker:
                rows.append('[FirstWorldFrameWitness] {"phase":"first_world_paint","world_epoch":4}\n')
        with tempfile.TemporaryDirectory() as temp:
            log = Path(temp) / "native.log"
            log.write_text("".join(rows), encoding="utf-8")
            return evaluate(scene, [log], expected_frames=3)

    def test_complete_nonresident_zero_revision_fields_pass(self):
        result = self.check()
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(result["ready_samples"], 3)

    def test_pending_or_false_physical_proof_and_wrong_sky_fail(self):
        for change in ("ready=0", "state=0", "state=1", "field_incarnation=0", "sky=0",
                       "field_revision=-1", "coord=(-1,0,2)", "block_light=16"):
            with self.subTest(change=change):
                self.assertEqual(self.check(change)["status"], "FAIL")

    def test_missing_paint_or_creature_coverage_cannot_pass(self):
        for options in (dict(omit=1), dict(marker=False), dict(consumer="item")):
            self.assertEqual(self.check(**options)["status"], "UNTESTED")
        self.assertEqual(self.check("physical=0")["status"], "UNTESTED")

    def test_boundary_emitters_do_not_weaken_independent_sky_reference(self):
        self.assertEqual(self.check("block_light=13")["status"], "PASS")
        self.assertEqual(self.check("block_light=13", z=51)["status"], "FAIL")
        self.assertEqual(self.check("sky=14 block_light=13")["status"], "FAIL")

    def test_unsupported_scene_has_no_acceptance(self):
        self.assertEqual(evaluate(dict(name="wet-negative-cave"), [])["status"], "UNTESTED")


if __name__ == "__main__":
    unittest.main()
