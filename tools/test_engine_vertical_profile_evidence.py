import copy
import json
from pathlib import Path
import tempfile
import unittest

from engine_smoke_fixtures import prepare_negative_cave
from engine_smoke_run import isolated_fixture, world_fingerprint
from engine_vertical_profile_evidence import evaluate


class VerticalProfileEvidenceTest(unittest.TestCase):
    def test_custom_fixture_changes_only_isolated_copy(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            runtime = root/"bin"
            source = runtime/"worlds/Saved"
            (source/"chunks").mkdir(parents=True)
            (runtime/"config.json").write_text("{}")
            (source/"world_data.json").write_text(json.dumps(dict(
                procedural=dict(min_height=-64, max_height=319))))
            (source/"users.json").write_text(json.dumps(dict(Username=dict(
                position=[22, 70, 51], player_creature_id=1))))
            (source/"creatures.json").write_text(json.dumps(dict(creatures=[dict(id=1, body_origin=[22, 68.38, 51])])))
            original = world_fingerprint(source)
            with isolated_fixture(runtime, "Saved", root/"artifacts") as (fixture, _):
                scene = prepare_negative_cave(fixture, 1, custom_profile=True)
                self.assertEqual(scene["starting_eye"], [22, -41.88, 51])
                self.assertEqual(scene["custom_vertical_profile"], dict(min_y=-48, max_y=255))
                self.assertEqual(json.loads((fixture/"world_data.json").read_text())["procedural"]["min_height"], -48)
                chunk = json.loads((fixture/"chunks/1_-3_3.json").read_text())
                cells = {(v["lx"], v["ly"], v["lz"]): v["type"] for v in chunk["voxels"]}
                self.assertEqual(cells[(6, 4, 3)], "stone")
                self.assertNotIn((6, 6, 3), cells)
                actor = json.loads((fixture/"creatures.json").read_text())["creatures"][0]
                self.assertEqual(actor["body_origin"], [22, -43.5, 51])
                self.assertEqual(world_fingerprint(source), original)
            self.assertEqual(world_fingerprint(source), original)
            self.assertFalse((runtime/".engine-smoke.lock").exists())

    def test_native_range_saved_pose_and_relocation_controls(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            scene = dict(custom_vertical_profile=dict(min_y=-48, max_y=255), starting_eye=[22, -41.88, 51])
            witness = dict(min_y=-48, max_y=255)
            report = dict(teleport_cruise=False, diagnostic_relocations_after_start=0,
                          diagnostic_bot=dict(state="complete"))
            rows = [dict(position=p) for p in ([22, -41.88, 51], [0, -35.88, 51], [-154, -35.88, 51])]
            def check(values=rows, native=witness):
                (root/"diagnostic_bot.jsonl").write_text("\n".join(json.dumps(r) for r in values))
                return evaluate(root, report, scene, native)
            self.assertEqual(check()["status"], "PASS")
            self.assertEqual(check(native=dict(min_y=-64, max_y=319))["status"], "FAIL")
            self.assertEqual(check(native=None)["status"], "UNTESTED")
            for index, y in ((0, 70), (1, 0), (1, -49), (1, float("nan"))):
                changed = copy.deepcopy(rows)
                changed[index]["position"][1] = y
                self.assertEqual(check(changed)["status"], "FAIL")
            report["diagnostic_relocations_after_start"] = 1
            self.assertEqual(check()["status"], "FAIL")


if __name__ == "__main__":
    unittest.main()
