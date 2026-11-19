import copy
import json
from pathlib import Path
import tempfile
import unittest
from engine_round_trip_evidence import evaluate


class RoundTripEvidenceTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.report = dict(teleport_cruise=False, diagnostic_relocations_after_start=0,
                           initial_resident_column_count=163, outside_initial_resident_samples=1000,
                           start_focus=[1,3], end_focus=[1,3],
                           diagnostic_bot=dict(enabled=True, state="complete", waypoints_visited=3))
        self.route = dict(relative=True, purpose="round_trip", waypoints=[
            dict(position=[0,6,0]),dict(position=[-304,6,0]),dict(position=[0,6,0])])
        self.poses = [dict(schema="diagnostic-bot-trace.v1", time=t, steady_tick_ms=tick, position=pos)
                      for t,tick,pos in [(0,1000,[22,-42,51]), (1,2000,[22,-36,51]),
                                         (4,5000,[-282,-36,51]), (8,9000,[22,-36,51]), (9,10000,[22,-36,51])]]
        self.events = [dict(source="runtime_owner", tick=1900, coord=(1,-3,3), origin=(1,-3,3), value=0, manager="ABCD")]
        for cy,old in [(-4,10),(-3,11)]: self.event("runtime_initial",1900,cy,old)
        for cy,old in [(-4,10),(-3,11)]: self.event("remove",3000,cy,old)
        self.event("disk_column_queued",7000,0,9)
        for cy,new in [(-4,20),(-3,21)]: self.event("create",7001,cy,new)
        self.event("disk_column_complete",7100,0,9)
        for cy,new in [(-4,20),(-3,21)]: self.event("runtime_final",10010,cy,new)
        self.summary = None

    def event(self, source, tick, cy, value):
        self.events.append(dict(source=source,tick=tick,coord=(1,cy,3),origin=(1,0,3),value=value,manager="ABCD"))

    def write(self):
        (self.root/"diagnostic_bot.jsonl").write_text("".join(json.dumps(p)+"\n" for p in self.poses),encoding="utf-8")
        rows = []
        for n,e in enumerate(self.events,1):
            triple = lambda x: ",".join(map(str,x))
            rows.append(f"[ChunkResidency] seq={n} tick_ms={e['tick']} source={e['source']} coord=({triple(e['coord'])}) origin=({triple(e['origin'])}) value={e['value']} manager={e['manager']}\n")
        summary = self.summary or f"[ChunkResidencySummary] buffered=1 reserved={len(rows)} published={len(rows)} dropped=0 unpublished=0 complete=1"
        (self.root/"round-trip-residency.log").write_text("".join(rows)+summary+"\n",encoding="utf-8")

    def check(self, status, reason=None):
        self.write()
        result = evaluate(self.root,self.report,self.route)
        self.assertEqual(result["status"],status,result)
        if reason: self.assertEqual(result["reason"],reason,result)
        return result

    def test_real_return_cohort_requires_more_than_endpoint_displacement(self):
        result=self.check("PASS")
        self.assertEqual(len(result["sections"]),2)
        self.assertEqual(result["disk_demand_id"],9)
        self.assertEqual(result["maximum_excursion_blocks"],304)
        self.assertIn("no pixel/light/performance",result["scope"])

    def test_stationary_bot_claim_does_not_prove_excursion(self):
        for p in self.poses: p["position"]=[22,-36,51]
        self.check("FAIL","round_trip_frontier_not_reached")

    def test_outward_only_is_not_a_return(self):
        for p in self.poses[3:]: p["position"]=[-282,-36,51]
        self.check("FAIL","round_trip_origin_not_regained")

    def test_native_relocation_is_failure(self):
        self.report["diagnostic_relocations_after_start"]=1
        self.check("FAIL","relocation_during_round_trip")

    def test_missing_native_audit_is_not_acceptance(self):
        self.report.pop("diagnostic_relocations_after_start")
        self.check("UNTESTED","missing_round_trip_native_audit")

    def test_missing_footprint_crossing_is_not_acceptance(self):
        self.report["outside_initial_resident_samples"]=0
        self.check("UNTESTED","missing_round_trip_native_audit")

    def test_incomplete_motor_route_is_failure(self):
        self.report["diagnostic_bot"]["waypoints_visited"]=2
        self.check("FAIL","round_trip_bot_incomplete")

    def test_held_original_section_cannot_claim_eviction(self):
        self.events=[e for e in self.events if not(e["source"]=="remove" and e["coord"][1]==-4)]
        self.check("FAIL","initial_cohort_not_evicted")

    def test_same_incarnation_cannot_claim_reload(self):
        for e in self.events:
            if e["source"] in ("create","runtime_final"): e["value"]-=10
        self.check("UNTESTED","missing_fresh_return_disk_cohort")

    def test_unrelated_final_generation_is_not_reload_witness(self):
        self.events[-1]["value"]=99
        self.check("UNTESTED","missing_fresh_return_disk_cohort")

    def test_load_completed_before_turnaround_is_not_return_reload(self):
        for e in self.events:
            if e["source"] in ("create","disk_column_queued","disk_column_complete"): e["tick"]-=3000
        self.check("UNTESTED","missing_fresh_return_disk_cohort")

    def test_mismatched_load_demand_is_not_acceptance(self):
        for e in self.events:
            if e["source"]=="disk_column_complete": e["value"]=99
        self.check("UNTESTED","missing_fresh_return_disk_cohort")

    def test_scratch_world_cannot_supply_runtime_reload(self):
        for e in self.events:
            if e["source"] in ("create","runtime_final"): e["manager"]="AAAA"
        self.check("UNTESTED","missing_fresh_return_disk_cohort")

    def test_duplicate_owner_is_ambiguous(self):
        self.events.insert(0,copy.deepcopy(self.events[0]))
        self.check("UNTESTED","missing_unique_runtime_owner")

    def test_dropped_or_unpublished_trace_is_not_absence_proof(self):
        for dropped,unpublished in [(1,0),(0,1)]:
            self.summary=f"[ChunkResidencySummary] buffered=1 reserved={len(self.events)+dropped+unpublished} published={len(self.events)} dropped={dropped} unpublished={unpublished} complete=0"
            self.check("UNTESTED","incomplete_round_trip_residency_export")

    def test_trace_count_mismatch_is_not_acceptance(self):
        self.summary="[ChunkResidencySummary] buffered=1 reserved=99 published=99 dropped=0 unpublished=0 complete=1"
        self.check("UNTESTED","incomplete_round_trip_residency_export")

    def test_missing_steady_clock_cannot_join_load_to_return(self):
        self.poses[0].pop("steady_tick_ms")
        self.check("FAIL","malformed_round_trip_evidence")

    def test_wrong_final_focus_column_is_failure(self):
        self.report["end_focus"]=[2,3]
        self.check("FAIL","round_trip_column_identity_mismatch")

    def test_missing_trace_is_untested(self):
        self.write()
        (self.root/"round-trip-residency.log").unlink()
        self.assertEqual(evaluate(self.root,self.report,self.route)["status"],"UNTESTED")


if __name__ == "__main__": unittest.main()
