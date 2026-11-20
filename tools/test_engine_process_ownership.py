"""Timeout cleanup must target the runner's child, never unrelated processes."""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
import flight_sim_run
from engine_smoke_run import isolated_fixture, world_fingerprint


class ProcessOwnershipTest(unittest.TestCase):
    def test_timeout_restores_config_and_preserves_source(self):
        with tempfile.TemporaryDirectory() as directory:
            runtime = Path(directory) / "runtime"
            world = runtime / "worlds/Saved"
            world.mkdir(parents=True)
            (world / "world_data.json").write_bytes(b'{"world_seed":42}')
            (world / "terrain.bin").write_bytes(b"original terrain")
            config = runtime / "config.json"
            original = b'{"original":true}\r\n'
            config.write_bytes(original)
            fingerprint = world_fingerprint(world)
            with self.assertRaises(TimeoutError):
                with isolated_fixture(runtime, "Saved", Path(directory) / "artifacts") as (fixture, _):
                    config.write_bytes(b"temporary config")
                    (fixture / "terrain.bin").write_bytes(b"changed copy")
                    code, monitor = flight_sim_run.run_with_timeout(
                        [sys.executable, "-c", "import time; time.sleep(30)"],
                        runtime, 0.1, sample_resources=False)
                    self.assertEqual(code, 124)
                    self.assertTrue(monitor["timed_out"])
                    raise TimeoutError("controlled timeout")
            self.assertEqual(config.read_bytes(), original)
            self.assertEqual(world_fingerprint(world), fingerprint)
            self.assertFalse((runtime / ".engine-smoke.lock").exists())
            self.assertEqual((fixture / "terrain.bin").read_bytes(), b"changed copy")

    def test_real_timeout_preserves_unrelated_process(self):
        sleeper = [sys.executable, "-c", "import time; time.sleep(30)"]
        unrelated = subprocess.Popen(sleeper)
        try:
            with tempfile.TemporaryDirectory() as directory:
                code, monitor = flight_sim_run.run_with_timeout(
                    sleeper, Path(directory), 0.1, sample_resources=False)
            self.assertEqual(code, 124)
            self.assertTrue(monitor["timed_out"])
            self.assertIsNone(unrelated.poll())
            self.assertFalse(flight_sim_run._owned_flight_processes)
        finally:
            if unrelated.poll() is None:
                unrelated.terminate()
            unrelated.wait(timeout=10)


if __name__ == "__main__":
    unittest.main()
