import json
from pathlib import Path
import tempfile
import unittest

from engine_surface_route_evidence import evaluate


class SurfaceEvidenceTest(unittest.TestCase):
    def test_motor_support_and_route_controls(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "bot-route.json").write_text(json.dumps(dict(movement_mode="walk")))
            rows = [dict(schema="diagnostic-bot-trace.v1", time=i,
                         position=[i*40, 2, 0], flying=0, up=0, down=0,
                         walking_ready=1, state="complete" if i == 4 else "travel") for i in range(5)]
            report = dict(teleport_cruise=False, diagnostic_relocations_after_start=0,
                          diagnostic_bot=dict(state="complete"))
            trace = root / "diagnostic_bot.jsonl"
            def check():
                trace.write_text("\n".join(json.dumps(row) for row in rows))
                return evaluate(root, report, minimum_chunks=8)
            self.assertEqual(check()["status"], "PASS")
            for field, value in (("flying", 1), ("up", 1), ("down", 1),
                                 ("position", [40, 3, 0]), ("position", [40, float("nan"), 0]),
                                 ("walking_ready", 0), ("time", -1)):
                with self.subTest(field=field, value=value):
                    original = rows[1][field]
                    rows[1][field] = value
                    self.assertEqual(check()["status"], "FAIL")
                    rows[1][field] = original
            report["diagnostic_relocations_after_start"] = 1
            self.assertEqual(check()["status"], "FAIL")
            trace.unlink()
            self.assertEqual(evaluate(root, report, minimum_chunks=8)["status"], "UNTESTED")


if __name__ == "__main__":
    unittest.main()
