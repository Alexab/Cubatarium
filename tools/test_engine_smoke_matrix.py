import json
from pathlib import Path
import tempfile
import unittest
from engine_smoke_matrix import execute


class MatrixTest(unittest.TestCase):
    def matrix(self):
        return dict(schema="engine-smoke-matrix.v1", cases=[
            dict(id="first", options={}), dict(id="second", options={})])

    def test_sequence_hashes_and_no_overwrite(self):
        calls = []
        def runner(world, destination, **options):
            calls.append(destination.name)
            destination.mkdir()
            verdict = dict(status="UNTESTED" if len(calls) == 2 else "PASS")
            (destination / "smoke-verdict.json").write_text(json.dumps(verdict))
            return verdict
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "matrix"
            result = execute(self.matrix(), output, "Saved", runner)
            self.assertEqual(calls, ["first", "second"])
            self.assertEqual(result["status"], "UNTESTED")
            self.assertTrue(all(len(row["sha256"]) == 64 for row in result["cases"]))
            with self.assertRaises(FileExistsError):
                execute(self.matrix(), output, "Saved", runner)

    def test_failure_and_interrupt_cannot_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            def failure(*args, **kwargs):
                raise RuntimeError("controlled failure")
            self.assertEqual(execute(self.matrix(), Path(directory)/"failure", "Saved", failure)["status"], "FAIL")
            def interrupt(*args, **kwargs):
                raise KeyboardInterrupt()
            output = Path(directory)/"interrupt"
            with self.assertRaises(KeyboardInterrupt):
                execute(self.matrix(), output, "Saved", interrupt)
            self.assertEqual(json.loads((output/"index.json").read_text())["status"], "UNTESTED")

    def test_traversal_is_rejected_before_output_creation(self):
        with tempfile.TemporaryDirectory() as directory:
            matrix = self.matrix()
            matrix["cases"][0]["id"] = "../escape"
            output = Path(directory)/"output"
            with self.assertRaises(ValueError):
                execute(matrix, output, "Saved")
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
