import tempfile
import unittest
from pathlib import Path

from engine_air_light_service import evaluate


class AirLightServiceTest(unittest.TestCase):
    def check(self, payload):
        with tempfile.TemporaryDirectory() as temp:
            log = Path(temp) / "runtime.log"
            log.write_text(payload, encoding="utf-8")
            return evaluate([log])

    def rows(self, columns=(0, 1, 2)):
        return "".join(
            f"[RelightAudit] job_submit job={i} column=({x},-3) air_requested=1\n"
            f"[RelightAudit] calculation_certificate job={i} sections=1 "
            "air_outputs=1 air_complete=1 inputs_complete=1\n"
            for i, x in enumerate(columns, 1))

    def test_requires_joined_distinct_column_receipts(self):
        self.assertEqual(self.check("")["status"], "UNTESTED")
        self.assertEqual(self.check(self.rows())["status"], "PASS")
        self.assertEqual(self.check(self.rows((0, 0, 0)))["status"], "UNTESTED")
        without_requests = "\n".join(r for r in self.rows().splitlines() if "job_submit" not in r)
        self.assertEqual(self.check(without_requests)["status"], "UNTESTED")
        self.assertEqual(self.check(without_requests)["unjoined_completions"], 3)

    def test_rejects_unrequested_and_impossible_receipts(self):
        for payload in [self.rows().replace("air_requested=1", "air_requested=0"),
                        self.rows().replace("air_complete=1", "air_complete=2"),
                        self.rows().replace("inputs_complete=1", "inputs_complete=0"),
                        self.rows().replace("air_outputs=1", "air_outputs=bad")]:
            with self.subTest(payload=payload):
                self.assertEqual(self.check(payload)["status"], "FAIL")

    def test_partial_or_repeated_receipts_do_not_grant_coverage(self):
        self.assertEqual(self.check(self.rows().replace("air_complete=1", "air_complete=0"))["status"], "UNTESTED")
        repeated = self.rows((0,)) * 3
        self.assertEqual(self.check(repeated)["joined_completed_jobs"], 1)
        self.assertEqual(self.check(repeated)["status"], "UNTESTED")


if __name__ == "__main__":
    unittest.main()
