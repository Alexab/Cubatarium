import copy
import unittest
import test_engine_round_trip_evidence as geometry
from engine_round_trip_evidence import evaluate


class AirRoundTripEvidenceTest(unittest.TestCase):
    event = geometry.RoundTripEvidenceTest.event
    write = geometry.RoundTripEvidenceTest.write

    def setUp(self):
        geometry.RoundTripEvidenceTest.setUp(self)
        self.event("runtime_air_initial", 1900, -2, 101)
        self.event("air_remove", 3000, -2, 101)
        self.event("air_install", 7001, -2, 201)
        self.event("runtime_air_final", 10010, -2, 201)
        self.events.sort(key=lambda e: e["tick"])

    def check(self, status, reason=None):
        self.write()
        result = evaluate(self.root, self.report, self.route, require_air=True)
        self.assertEqual(result["status"], status, result)
        if reason:
            self.assertEqual(result["reason"], reason, result)
        return result

    def test_actual_air_owner_changes_under_same_disk_demand(self):
        result = self.check("PASS")
        self.assertEqual(result["air_sections"], [dict(coord=[1, -2, 3],
            retired_field_incarnation=101, returned_field_incarnation=201)])

    def test_missing_air_does_not_inherit_geometry_pass(self):
        self.events = [e for e in self.events if "air" not in e["source"]]
        self.check("UNTESTED", "missing_initial_or_final_air_cohort")

    def test_held_air_owner_is_not_eviction(self):
        self.events = [e for e in self.events if e["source"] != "air_remove"]
        self.check("FAIL", "initial_air_cohort_not_evicted")

    def test_reused_air_owner_is_a_contradiction(self):
        for e in self.events:
            if e["source"] in ("air_install", "runtime_air_final"):
                e["value"] = 101
        self.check("FAIL", "returned_air_reuses_retired_owner")

    def test_final_air_needs_actual_disk_install(self):
        self.events = [e for e in self.events if e["source"] != "air_install"]
        self.check("UNTESTED", "missing_fresh_return_disk_cohort")

    def test_materializing_a_temporary_holder_is_not_allowed(self):
        self.event("air_materialize", 8000, -2, 201)
        self.events.sort(key=lambda e: e["tick"])
        self.check("FAIL", "air_cohort_materialized_geometry")

    def test_unrelated_manager_cannot_supply_air_install(self):
        for e in self.events:
            if e["source"] == "air_install":
                e["manager"] = "EEEE"
        self.check("UNTESTED", "missing_fresh_return_disk_cohort")

    def test_snapshot_does_not_override_real_geometry(self):
        self.event("runtime_initial", 1900, -2, 55)
        self.event("remove", 3000, -2, 55)
        self.event("create", 7001, -2, 66)
        self.event("runtime_final", 10010, -2, 66)
        self.events.sort(key=lambda e: e["tick"])
        self.check("FAIL", "air_cohort_has_geometry")

    def test_duplicate_air_snapshot_is_not_complete_evidence(self):
        self.events.append(copy.deepcopy(self.events[-1]))
        self.check("UNTESTED", "missing_initial_or_final_air_cohort")

    def test_missing_buffer_export_is_not_proof(self):
        self.summary = f"[ChunkResidencySummary] buffered=1 reserved={len(self.events)+1} published={len(self.events)} dropped=1 unpublished=0 complete=0"
        self.check("UNTESTED", "incomplete_round_trip_residency_export")
