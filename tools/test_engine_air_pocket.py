import copy
import json
import tempfile
import unittest
from pathlib import Path

from engine_air_pocket import BOX, START, prepare, static_config, evaluate


class AirPocketTest(unittest.TestCase):
    def test_fixture_geometry_saved_pose_and_isolation(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            original = root/"worlds/Original"
            original.mkdir(parents=True)
            (original/"marker").write_bytes(b"preserve")
            with self.assertRaises(ValueError):
                prepare(original, 1)
            self.assertEqual((original/"marker").read_bytes(), b"preserve")
            world = root/"worlds/EngineSmoke_test"
            (world/"chunks").mkdir(parents=True)
            (world/"world_data.json").write_text(json.dumps({"procedural":{"min_height":-64,"max_height":319,"fill_water":True}}))
            (world/"users.json").write_text(json.dumps({"Username":{"player_creature_id":1}}))
            (world/"creatures.json").write_text(json.dumps({"creatures":[{"id":1}]}))
            binary = world/"chunks/1_-3_3.cchunk"
            binary.write_bytes(b"old frame")
            scene = prepare(world, 1)
            self.assertFalse(binary.exists())
            self.assertEqual(scene["air_pocket_box"], BOX)
            self.assertEqual(scene["carved_water_cells"], 150)
            frame = json.loads((world/"chunks/1_-3_3.json").read_text())
            voxels = {(16+v["lx"],-48+v["ly"],48+v["lz"]):v["type"] for v in frame["voxels"]}
            for x in range(20,25):
                for y in range(-43,-37):
                    for z in range(49,54):
                        self.assertNotIn((x,y,z), voxels)
            self.assertEqual(voxels[22,-37,51], "water")
            self.assertEqual(voxels[26,-42,51], "water")
            self.assertEqual(voxels[22,-44,51], "stone")
            self.assertEqual(json.loads((world/"users.json").read_text())["Username"]["position"], START)
            self.assertEqual(json.loads((world/"creatures.json").read_text())["creatures"][0]["body_origin"], [22,-43.5,51])
            self.assertIs(json.loads((world/"world_data.json").read_text())["procedural"]["fill_water"], False)
            config = json.loads(static_config(b'{"physics":{"profile":"standard","enable_fluids":true},"keep":7}'))
            self.assertIs(config["physics"]["enable_fluids"], False)
            self.assertEqual(config["keep"], 7)

    def test_evidence_and_negative_controls(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            (folder/"config.air-pocket.json").write_bytes(static_config(b'{}'))
            scene = dict(name="air-pocket-negative-cave", starting_eye=START,
                         air_pocket_box=BOX, carved_water_cells=150)
            report = dict(exit_code=0, teleport_cruise=False, diagnostic_relocations_after_start=0)
            rows = [dict(schema="diagnostic-bot-trace.v1", time=i, position=START,
                         eye_in_fluid=False, air_pocket_above_in_fluid=True,
                         air_pocket_side_in_fluid=True) for i in range(12)]
            def check(data=rows, native=report):
                (folder/"diagnostic_bot.jsonl").write_text("".join(json.dumps(r)+"\n" for r in data))
                return evaluate(folder, native, scene)["status"]
            self.assertEqual(check(), "PASS")
            for key,value in [("eye_in_fluid",True),("air_pocket_above_in_fluid",False),
                              ("air_pocket_side_in_fluid",False),("position",[22,64,51]),
                              ("position",[float('nan'),-41.88,51])]:
                bad = copy.deepcopy(rows); bad[2][key] = value
                self.assertEqual(check(bad), "FAIL")
            self.assertNotEqual(check(rows[:2]), "PASS")
            missing = dict(report); missing.pop("diagnostic_relocations_after_start")
            self.assertEqual(check(native=missing), "FAIL")
            moved = dict(report, diagnostic_relocations_after_start=1)
            self.assertEqual(check(native=moved), "FAIL")
            repeated = copy.deepcopy(rows); repeated[1]["time"]=0
            self.assertEqual(check(repeated), "FAIL")


if __name__ == "__main__":
    unittest.main()
