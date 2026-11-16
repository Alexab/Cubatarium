"""Fixture isolation must survive failed/interrupted diagnostic runs."""
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

from engine_smoke_run import isolated_fixture, world_fingerprint
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


if __name__ == "__main__":
    unittest.main()
