import copy
import unittest

from engine_air_column_evidence import evaluate
import test_engine_round_trip_evidence as primary_fixture


class AirColumnEvidenceTest(unittest.TestCase):
    def setUp(self):
        self.base = primary_fixture.RoundTripEvidenceTest("test_real_return_cohort_requires_more_than_endpoint_displacement")
        self.base.setUp()
        self.addCleanup(self.base.doCleanups)
        for source, tick, value in [("runtime_air_initial", 1900, 31), ("air_remove", 3000, 31),
                                    ("air_install", 7001, 41), ("runtime_air_final", 10010, 41)]:
            self.base.event(source, tick, -2, value)
        self.base.events.sort(key=lambda e: e["tick"])
        self.base.write()
        self.path = self.base.root / "secondary-column-residency.log"
        self.events = []
        self.add("runtime_owner", 1900, -3, 0, origin=(1, -3, 3))
        for cy in range(-4, 20):
            self.add("runtime_air_initial", 1900, cy, cy + 100)
            self.add("runtime_air_decoded_initial", 1900, cy, cy + 100)
        for cy in range(-4, 20):
            self.add("air_remove", 3000, cy, cy + 100)
        self.add("disk_column_queued", 7000, 0, 19)
        for cy in range(-4, 20):
            self.add("air_install", 7001, cy, cy + 200)
        self.add("disk_column_complete", 7100, 0, 19)
        for cy in range(-4, 20):
            self.add("runtime_air_final", 10010, cy, cy + 200)
            self.add("runtime_air_decoded_final", 10010, cy, cy + 200)

    def add(self, source, tick, cy, value, **extra):
        self.events.append(dict(source=source, tick=tick, coord=(1, cy, 5),
                                origin=(1, 0, 5), value=value, manager="ABCD") | extra)

    def write(self, summary=None):
        rows = []
        for n, e in enumerate(self.events, 1):
            triple = lambda v: ','.join(map(str, v))
            rows.append(f"[ChunkResidency] seq={n} tick_ms={e['tick']} source={e['source']} "
                        f"coord=({triple(e['coord'])}) origin=({triple(e['origin'])}) "
                        f"value={e['value']} manager={e['manager']}\n")
        if summary is None:
            summary = (f"[ChunkResidencySummary] buffered=1 reserved={len(rows)} "
                       f"published={len(rows)} dropped=0 unpublished=0 complete=1\n")
        self.path.write_text(''.join(rows) + summary, encoding="utf-8")

    def check(self, status, reason=None, **kwargs):
        result = evaluate(self.base.root, self.base.report, self.base.route,
                          **(dict(column=(1, 5), min_chunk_y=-4, max_chunk_y=19) | kwargs))
        self.assertEqual(result["status"], status, result)
        if reason:
            self.assertEqual(result["reason"], reason, result)
        return result

    def test_full_profile_requires_all_twenty_four_decoded_slices(self):
        self.write()
        result = self.check("PASS")
        self.assertEqual(len(result["sections"]), 24)
        self.assertEqual(result["disk_demand_id"], 19)
        self.assertEqual(result["primary"]["status"], "PASS")
        self.assertIn("no pixel/light/weather", result["scope"])

    def test_missing_any_data_or_field_snapshot_cannot_pass(self):
        original = copy.deepcopy(self.events)
        for source in ("runtime_air_initial", "runtime_air_final", "runtime_air_decoded_initial",
                       "runtime_air_decoded_final"):
            for cy in (-4, 0, 19):
                with self.subTest(source=source, cy=cy):
                    self.events = [e for e in original if not (e["source"] == source and e["coord"][1] == cy)]
                    self.write()
                    self.check("UNTESTED", "missing_full_profile_decoded_air_cohort")

    def test_geometry_materialization_is_failure(self):
        original = copy.deepcopy(self.events)
        for source in ("create", "air_materialize", "runtime_initial", "runtime_final"):
            self.events = copy.deepcopy(original)
            self.add(source, 7002, 0, 300)
            self.events.sort(key=lambda e: e["tick"])
            self.write()
            self.check("FAIL", "air_data_column_materialized_geometry")

    def test_held_or_reused_owner_is_failure(self):
        original = copy.deepcopy(self.events)
        self.events = [e for e in original if not (e["source"] == "air_remove" and e["coord"][1] == 19)]
        self.write()
        self.check("FAIL", "air_data_column_not_retired")
        self.events = copy.deepcopy(original)
        for e in self.events:
            if e["source"] in ("runtime_air_final", "runtime_air_decoded_final"):
                e["value"] -= 100
        self.write()
        self.check("FAIL", "air_data_column_reused_retired_field")

    def test_indexed_absence_or_stale_decoded_owner_cannot_pass(self):
        self.events[-1]["value"] += 1
        self.write()
        self.check("FAIL", "decoded_air_field_identity_mismatch")

    def test_return_requires_actual_matching_disk_demand(self):
        original = copy.deepcopy(self.events)
        for source in ("disk_column_queued", "disk_column_complete", "air_install"):
            self.events = [e for e in original if e["source"] != source]
            self.write()
            self.check("UNTESTED", "missing_fresh_air_column_disk_cohort")
        self.events = copy.deepcopy(original)
        for e in self.events:
            if e["source"] == "disk_column_complete":
                e["value"] = 999
        self.write()
        self.check("UNTESTED", "missing_fresh_air_column_disk_cohort")

    def test_scratch_manager_cannot_supply_disk_reload(self):
        for e in self.events:
            if e["source"] in ("disk_column_queued", "disk_column_complete", "air_install"):
                e["manager"] = "AAAA"
        self.write()
        self.check("UNTESTED", "missing_fresh_air_column_disk_cohort")

    def test_secondary_owner_must_join_primary_runtime(self):
        original = copy.deepcopy(self.events)
        for key, value in [("manager", "AAAA"), ("origin", (1, -3, 5)), ("coord", (2, -3, 5))]:
            self.events = copy.deepcopy(original)
            self.events[0][key] = value
            self.write()
            self.check("FAIL", "secondary_air_owner_identity_mismatch")

    def test_shared_loss_or_missing_summary_cannot_prove_absence(self):
        for dropped, unpublished in ((9, 0), (0, 1)):
            count = len(self.events)
            self.write(f"[ChunkResidencySummary] buffered=1 reserved={count+dropped+unpublished} "
                       f"published={count} dropped={dropped} unpublished={unpublished} complete=0\n")
            self.check("UNTESTED", "incomplete_secondary_air_export")
        self.write("")
        self.check("UNTESTED", "incomplete_secondary_air_export")

    def test_malformed_sequence_and_late_rows_cannot_pass(self):
        self.write()
        self.path.write_text(self.path.read_text(encoding="utf-8").replace("seq=2 ", "seq=1 "), encoding="utf-8")
        self.check("FAIL", "malformed_secondary_air_evidence")
        self.write()
        with self.path.open("a", encoding="utf-8") as f:
            f.write("late\n")
        self.check("FAIL", "malformed_secondary_air_evidence")

    def test_primary_geometry_proof_is_still_required(self):
        self.write()
        self.base.events = [e for e in self.base.events if e["source"] != "runtime_initial"]
        self.base.write()
        self.check("UNTESTED", "primary_round_trip_not_proven")

    def test_profile_shape_and_missing_trace(self):
        self.check("UNTESTED", "missing_secondary_air_trace")
        for kwargs in (dict(column=[True, 5]), dict(min_chunk_y=20), dict(max_chunk_y=4096),
                       dict(column=[1, 5, 6])):
            self.check("FAIL", "invalid_declared_air_profile", **kwargs)


if __name__ == "__main__":
    unittest.main()
