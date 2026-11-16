"""Fixture isolation must survive failed/interrupted diagnostic runs."""
import tempfile
import unittest
import json
import struct
import zlib
from pathlib import Path
from unittest.mock import Mock, patch

from engine_smoke_run import isolated_fixture, world_fingerprint
from engine_smoke_run import valid_png, inspection_evidence
import engine_smoke_run


class IsolatedFixtureTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.runtime = self.root / "bin"
        self.world = self.runtime / "worlds" / "Saved"
        self.world.mkdir(parents=True)
        (self.world / "world_data.json").write_text('{"world_seed":42}')
        (self.world / "chunks").mkdir()
        (self.world / "chunks" / "0_0_0.cchunk").write_bytes(b"fixture chunk")
        self.config = self.runtime / "config.json"
        self.original = b'{"original": true}\r\n'
        self.config.write_bytes(self.original)
        self.artifacts = self.root / "artifacts"

    def test_failure_restores_config_and_preserves_original_world(self):
        fingerprint = world_fingerprint(self.world)
        fixture = None
        with self.assertRaisesRegex(RuntimeError, "simulated"):
            with isolated_fixture(self.runtime, "Saved", self.artifacts) as (fixture, source_hash):
                self.assertEqual(source_hash, fingerprint)
                self.assertEqual(world_fingerprint(fixture), fingerprint)
                self.config.write_bytes(b"changed by app")
                (fixture / "chunks" / "new.cchunk").write_bytes(b"generated")
                raise RuntimeError("simulated child failure")
        self.assertEqual(world_fingerprint(self.world), fingerprint)
        self.assertEqual(self.config.read_bytes(), self.original)
        self.assertFalse((self.runtime / ".engine-smoke.lock").exists())
        self.assertTrue((fixture / "chunks" / "new.cchunk").exists())
        self.assertEqual((self.artifacts / "config.original.json").read_bytes(), self.original)

    def test_second_runner_cannot_take_live_lease(self):
        with isolated_fixture(self.runtime, "Saved", self.artifacts):
            with self.assertRaises(FileExistsError):
                with isolated_fixture(self.runtime, "Saved", self.root / "second"):
                    self.fail("another runner acquired a live lease")
            self.assertTrue((self.runtime / ".engine-smoke.lock").exists())

    def test_artifacts_cannot_contaminate_source_save(self):
        with self.assertRaises(ValueError):
            with isolated_fixture(self.runtime, "Saved", self.world / "artifacts"):
                self.fail("nested output accepted")

    def test_traversal_rejected(self):
        for name in ["../Saved", "..", "Saved/sub", "Saved\\sub", "X:Saved"]:
            with self.subTest(name=name), self.assertRaises(ValueError):
                with isolated_fixture(self.runtime, name, self.artifacts):
                    self.fail("unsafe world name accepted")

    def test_existing_artifacts_are_never_overwritten(self):
        self.artifacts.mkdir()
        with self.assertRaises(FileExistsError):
            with isolated_fixture(self.runtime, "Saved", self.artifacts):
                self.fail("reused artifact directory")

    def test_interrupt_stops_owned_runner_before_restoring_config(self):
        (self.runtime / "Cubatarium.exe").write_bytes(b"test executable identity")
        child = Mock(pid=123)
        def wait():
            self.config.write_bytes(b"child changed settings")
            raise KeyboardInterrupt()
        calls = 0
        def interrupt_once(*args, **kwargs):
            nonlocal calls
            calls += 1
            if calls == 1:
                wait()
            return 0
        child.wait.side_effect = interrupt_once
        with patch.object(engine_smoke_run, "ROOT", self.root), \
             patch("flight_sim_run.exe_writable", return_value=True), \
             patch.object(engine_smoke_run.subprocess, "check_output", return_value="test-sha"), \
             patch.object(engine_smoke_run.subprocess, "Popen", return_value=child), \
             patch("flight_sim_run.kill_process_tree") as kill:
            with self.assertRaises(KeyboardInterrupt):
                engine_smoke_run.run("Saved", self.artifacts)
            kill.assert_called_once_with(123)
        self.assertEqual(self.config.read_bytes(), self.original)
        self.assertFalse((self.runtime / ".engine-smoke.lock").exists())

    def png(self):
        def chunk(kind, data):
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
        return (b"\x89PNG\r\n\x1a\n" +
                chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(b"\0\0\0\0\xff")) + chunk(b"IEND", b""))

    def test_truncated_and_corrupt_png_are_not_evidence(self):
        path = self.root / "capture.png"
        payload = self.png()
        path.write_bytes(payload)
        self.assertTrue(valid_png(path))
        path.write_bytes(payload[:-3])
        self.assertFalse(valid_png(path))
        corrupt = bytearray(payload)
        corrupt[20] ^= 1
        path.write_bytes(corrupt)
        self.assertFalse(valid_png(path))

    def test_inspection_requires_distinct_viewpoints_and_written_images(self):
        (self.artifacts / "captures").mkdir(parents=True)
        rows = []
        for i in range(12):
            rows.append(dict(capture_request=1, capture_id=i, yaw=90 * (i // 3),
                             pitch=-45 + 45 * (i % 3)))
            (self.artifacts / "captures" / f"inspection_{i:04}.png").write_bytes(self.png())
        trace = self.artifacts / "diagnostic_bot.jsonl"
        trace.write_text("\n".join(json.dumps(row) for row in rows))
        report = dict(diagnostic_bot=dict(inspection_capture_requested_n=12))
        self.assertEqual(inspection_evidence(self.artifacts, report)["verified"], 12)
        (self.artifacts / "captures" / "inspection_0005.png").unlink()
        self.assertEqual(inspection_evidence(self.artifacts, report)["verified"], 11)
        for row in rows:
            row["yaw"] = 0
        trace.write_text("\n".join(json.dumps(row) for row in rows))
        self.assertLess(inspection_evidence(self.artifacts, report)["verified"], 12)


class NegativeCaveFixtureTest(unittest.TestCase):
    def test_scene_is_prepared_only_in_clone_and_matches_analytic_geometry(self):
        from engine_smoke_fixtures import prepare_dry_negative_cave, prepare_negative_cave
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            runtime = root / "bin"
            source = runtime / "worlds" / "Saved"
            (source / "chunks").mkdir(parents=True)
            (runtime / "config.json").write_text("{}")
            (source / "world_data.json").write_text(json.dumps({
                "procedural": {"min_height": -64, "max_height": 319}}))
            (source / "users.json").write_text(json.dumps({"Username": {"position": [1, 65, 1]}}))
            (source / "chunks" / "0_-3_3.cchunk").write_bytes(b"original")
            before = world_fingerprint(source)
            with isolated_fixture(runtime, "Saved", root / "evidence") as (fixture, _):
                scene = prepare_dry_negative_cave(fixture, 1)
                self.assertFalse((fixture / "chunks" / "0_-3_3.cchunk").exists())
                payload = json.loads((fixture / "chunks" / "0_-3_3.json").read_text())
                voxels = {(v["lx"], v["ly"] - 48, v["lz"] + 48): v["type"]
                          for v in payload["voxels"]}
                self.assertEqual(voxels[(8, -44, 51)], "stone")
                self.assertNotIn((8, -40, 51), voxels)
                self.assertEqual(scene["starting_eye"][1], -41.88)
                ocean = json.loads((fixture / "chunks" / "0_3_3.json").read_text())
                self.assertTrue(any(v["type"] == "water" and v["ly"] == 15 for v in ocean["voxels"]))
                wet = prepare_negative_cave(fixture, 1, flooded=True)
                flooded = json.loads((fixture / "chunks" / "0_-3_3.json").read_text())
                self.assertTrue(wet["expected_eye_in_fluid"])
                self.assertTrue(any(v["lx"] == 8 and v["ly"] == 8 and v["lz"] == 3
                                    and v["type"] == "water" for v in flooded["voxels"]))
            self.assertEqual(world_fingerprint(source), before)
            with self.assertRaises(ValueError):
                prepare_dry_negative_cave(source, 1)

    def test_trace_rejects_recovery_teleport_wet_classification_and_missing_witness(self):
        from engine_smoke_fixtures import evaluate_fixture_trace
        with tempfile.TemporaryDirectory() as temp:
            trace = Path(temp) / "trace.jsonl"
            scene = {"starting_eye": [22, -41.88, 51], "expected_max_eye_y": -30.5,
                     "expected_eye_in_fluid": False}
            rows = [{"schema": "diagnostic-bot-trace.v1", "position": [22, -41.88, 51],
                     "eye_in_fluid": False},
                    {"schema": "diagnostic-bot-trace.v1", "position": [-40, -36, 51],
                     "eye_in_fluid": False}]
            def check():
                trace.write_text("\n".join(json.dumps(r) for r in rows))
                return evaluate_fixture_trace(scene, trace)
            self.assertEqual(check()["status"], "PASS")
            scene["expected_eye_in_fluid"] = True
            self.assertEqual(check()["reason"], "wet_cave_classified_as_air")
            for row in rows:
                row["eye_in_fluid"] = True
            self.assertEqual(check()["reason"], "negative_wet_cave_observed")
            scene["expected_eye_in_fluid"] = False
            for row in rows:
                row["eye_in_fluid"] = False
            rows[1]["eye_in_fluid"] = True
            self.assertEqual(check()["reason"], "dry_cave_classified_as_fluid")
            del rows[1]["eye_in_fluid"]
            self.assertEqual(check()["status"], "UNTESTED")
            rows[1]["position"][1] = 68
            self.assertEqual(check()["reason"], "camera_left_negative_cave")
            rows[0]["position"][0] = 25
            self.assertEqual(check()["reason"], "fixture_start_position_changed")


if __name__ == "__main__":
    unittest.main()
