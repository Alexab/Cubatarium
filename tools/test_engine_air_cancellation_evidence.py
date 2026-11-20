import copy
import unittest

import test_engine_air_column_evidence as air_fixture
from engine_air_cancellation_evidence import evaluate


class AirCancellationEvidenceTest(unittest.TestCase):
    def setUp(self):
        self.base = air_fixture.AirColumnEvidenceTest("test_full_profile_requires_all_twenty_four_decoded_slices")
        self.base.setUp()
        self.addCleanup(self.base.doCleanups)
        self.events = self.base.events
        before = len(self.events)
        for source, tick, cy, value in [
            ("disk_column_queued", 6000, 0, 18), ("air_install", 6001, -3, 888),
            ("disk_air_owned", 6001, -3, 18), ("disk_air_probe_cancel", 6002, 0, 18),
            ("air_remove", 6002, -3, 888), ("disk_air_cancelled", 6002, 0, 18),
            ("disk_result_cancelled", 7000, 1, 18)]:
            self.base.add(source, tick, cy, value)
        self.events.sort(key=lambda e: e["tick"])
        self.assertGreater(len(self.events), before)

    def check(self, status, reason=None):
        self.base.events = self.events
        self.base.write()
        result = evaluate(self.base.base.root, self.base.base.report, self.base.base.route,
                          column=(1, 5), min_chunk_y=-4, max_chunk_y=19)
        self.assertEqual(result["status"], status, result)
        if reason:
            self.assertEqual(result["reason"], reason, result)
        return result

    def test_actual_partial_owner_and_late_result_join_to_fresh_return(self):
        result = self.check("PASS")
        self.assertEqual(result["cancelled_demand_id"], 18)
        self.assertEqual(result["returned_demand_id"], 19)
        self.assertEqual(result["discarded_result_count"], 1)
        self.assertEqual(result["cancelled_sections"][0]["cancelled_field_incarnation"], 888)

    def test_probe_request_or_full_return_alone_is_not_cancellation(self):
        self.events = [e for e in self.events if e["source"] != "disk_air_probe_cancel"]
        self.check("UNTESTED", "no_actual_partial_cancellation")

    def test_missing_retirement_is_failure(self):
        self.events = [e for e in self.events if not (e["source"] == "air_remove" and e["value"] == 888)]
        self.check("FAIL", "cancelled_air_owner_not_retired")

    def test_cancelled_request_cannot_claim_success(self):
        self.events.insert(-1, dict(self.events[-1], source="disk_column_complete", value=18))
        self.check("FAIL", "cancelled_demand_claimed_complete")

    def test_actual_late_result_discard_is_required(self):
        self.events = [e for e in self.events if e["source"] != "disk_result_cancelled"]
        self.check("UNTESTED", "missing_actual_cancelled_result_discard")

    def test_unknown_field_or_wrong_demand_cannot_claim_owner(self):
        for e in self.events:
            if e["source"] == "disk_air_owned":
                e["coord"] = (1, 9, 5)
        self.check("FAIL", "cancelled_air_install_identity_mismatch")

    def test_duplicate_cancellation_is_ambiguous(self):
        e = next(e for e in self.events if e["source"] == "disk_air_probe_cancel")
        self.events.insert(self.events.index(e), copy.deepcopy(e))
        self.check("FAIL", "ambiguous_probe_cancellation")

    def test_cancel_before_excursion_is_not_returning_load(self):
        for e in self.events:
            if e["source"] == "disk_column_queued" and e["value"] == 18:
                e["tick"] = 4000
        self.check("FAIL", "cancellation_not_on_returning_partial_demand")

    def test_same_cancelled_and_final_demand_cannot_pass(self):
        for e in self.events:
            if e["source"] == "disk_air_probe_cancel":
                e["value"] = 19
        self.check("FAIL", "cancelled_and_returned_demand_identity_mismatch")


if __name__ == "__main__":
    unittest.main()
