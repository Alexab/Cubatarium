import unittest
from engine_verdict_contract import CORE_COMPONENTS, finalize, required_for_options


class VerdictContractTest(unittest.TestCase):
    def test_malformed_containers_and_status_types_fail_closed(self):
        for parts in (None, [], "PASS"):
            self.assertEqual(finalize({"components": parts})["status"], "FAIL")
        for invalid in ([], {}, True, None):
            verdict = self.passing()
            verdict["components"]["runtime"]["status"] = invalid
            self.assertEqual(finalize(verdict)["status"], "FAIL")

    def test_every_explicit_probe_needs_independent_component(self):
        for option in ("read_failure", "air_cancellation", "terrain_save_failure",
                       "world_actions", "synchronous_meshing", "capture_backpressure"):
            with self.subTest(option=option):
                required = required_for_options(**{option: True})
                self.assertTrue(required)
                verdict = finalize(self.passing(), required)
                self.assertEqual(verdict["status"], "UNTESTED")
                self.assertTrue(all(verdict["components"][name]["status"] == "UNTESTED"
                                    for name in required))

    def passing(self):
        return {"components": {name: {"status": "PASS"} for name in CORE_COMPONENTS}}

    def test_missing_performance_cannot_pass(self):
        verdict = self.passing()
        del verdict["components"]["performance"]
        self.assertEqual(finalize(verdict)["status"], "UNTESTED")
        self.assertEqual(verdict["components"]["visual"]["status"], "PASS")

    def test_requested_fault_requires_own_evidence(self):
        verdict = finalize(self.passing(), ["disk_read_failure"])
        self.assertEqual(verdict["status"], "UNTESTED")
        verdict["components"]["disk_read_failure"] = {"status": "FAIL"}
        self.assertEqual(finalize(verdict)["status"], "FAIL")

    def test_malformed_status_fails_and_complete_components_pass(self):
        self.assertEqual(finalize(self.passing())["status"], "PASS")
        verdict = self.passing()
        verdict["components"]["runtime"]["status"] = "UNKNOWN"
        self.assertEqual(finalize(verdict)["status"], "FAIL")


if __name__ == "__main__":
    unittest.main()
