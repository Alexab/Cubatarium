import json
from pathlib import Path
import tempfile
import unittest

from analyze_mesh_publication import analyze


class PublicationDiagnosticsTest(unittest.TestCase):
    def run_trace(self, files, faults=()):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "logs").mkdir()
            for name, lines in files.items():
                (root / "logs" / name).write_text("\n".join(lines), encoding="utf-8")
            verdict = {"status": "FAIL", "components": {"fixture_opaque_depth": {
                "fault_attribution": {"sections": list(faults)}}}}
            original = json.dumps(verdict)
            report = analyze(root, verdict)
            self.assertEqual(json.dumps(verdict), original)
            return report

    def watch(self, frame, revision=2, **overrides):
        fields = dict(coord="(-5,-3,3)", frame_epoch=frame, revision=revision,
                      queue_kind=1, queue_age=frame, inflight=0, capture_pending=0,
                      awaiting_light=0, light_refresh=0)
        fields.update(overrides)
        return "I20261119 11:16:23.0 13720 Log.cpp:601] [ChunkPublication] watch " + " ".join(
            f"{key}={value}" for key, value in fields.items())

    def test_rotation_joins_consecutive_samples_and_retains_depth_failure(self):
        fault = dict(coord=[-5, -3, 3], faults=78, no_published_geometry=78,
                     first_frame_epoch=360, last_frame_epoch=362)
        report = self.run_trace({
            "game.log.INFO.001": [self.watch(360), self.watch(361)],
            "game.log.INFO.002": [self.watch(362),
                "prefix] [ScreenRayRepair] frame=362 coord=-5,-3,3 action=first_mesh_ticket_rejected",
                "prefix] [ChunkPublication] resource coord=(-5,-3,3) frame_epoch=448 decision=prepared"],
        }, [fault])
        section = report["sections"][0]
        self.assertEqual(section["longest_queue_only_run"]["samples"], 3)
        self.assertEqual(section["depth_fault_attribution"], fault)
        self.assertEqual(section["repair_actions"]["first_mesh_ticket_rejected"], 1)
        self.assertEqual(section["timeline"][0]["event"], "resource")
        self.assertNotIn("published", section)
        self.assertEqual([row["frame_epoch"] for row in section["fault_boundary_watch"]],
                         [360, 362])

    def test_active_source_can_be_waiting_for_light_instead_of_a_cpu_worker(self):
        fault = dict(coord=[-5, -3, 3], faults=2, first_frame_epoch=269,
                     last_frame_epoch=269, work_owner_flag_counts={"2": 2})
        report = self.run_trace({"game.log.INFO.001": [
            self.watch(269, queue_kind=0, inflight=1, awaiting_light=1),
            self.watch(269, queue_kind=0, inflight=1, awaiting_light=1),
            self.watch(269, queue_kind=0, inflight=1, awaiting_light=0),
        ]}, [fault])
        section = report["sections"][0]
        self.assertEqual(section["queue_only_samples"], 0)
        self.assertEqual(section["fault_boundary_watch"][0]["awaiting_light"], 1)
        self.assertEqual(len(section["fault_boundary_watch"]), 1)
        self.assertEqual(section["fault_boundary_watch_conflicts"], 1)
        self.assertEqual(section["depth_fault_attribution"]["work_owner_flag_counts"], {"2": 2})
        self.assertNotIn("cpu_builder", section)
        self.assertEqual(len(report["analyzer_sha256"]), 64)

    def test_unknown_watch_owners_do_not_become_zero(self):
        report = self.run_trace({"game.log.INFO.001": [
            self.watch(1), self.watch(2, inflight="unknown"), self.watch(3),
            self.watch(4, awaiting_light=1), self.watch(5),
            self.watch(6).replace(" light_refresh=0", ""), self.watch(7),
        ]})
        section = report["sections"][0]
        self.assertEqual(report["malformed_rows"], 2)
        self.assertEqual(section["queue_only_samples"], 4)
        self.assertEqual(section["longest_queue_only_run"]["samples"], 1)

    def test_gaps_revision_changes_and_unqueued_samples_break_runs(self):
        report = self.run_trace({"game.log.INFO.001": [
            self.watch(1), self.watch(3), self.watch(4, revision=3),
            self.watch(5, revision=3), self.watch(6, revision=3, queue_kind=0),
            self.watch(7, revision=3),
        ]})
        self.assertEqual(report["sections"][0]["longest_queue_only_run"], {
            "first_frame_epoch": 4, "last_frame_epoch": 5, "samples": 2, "revision": 3})

    def test_missing_trace_is_unknown_even_with_independent_faults(self):
        fault = dict(coord=[1, 2, 3], faults=3)
        report = self.run_trace({}, [fault])
        self.assertEqual(report["log_files"], 0)
        self.assertFalse(report["sections"][0]["trace_observed"])
        self.assertNotIn("longest_queue_only_run", report["sections"][0])

    def test_incomplete_field_handoff_joins_delayed_ready_data_without_changing_fault(self):
        fault = dict(coord=[-5, -3, 3], faults=4)
        report = self.run_trace({"game.log.INFO.001": [
            "[WorldColumnSource] source=disk outcome=queued coord=(-6,0,4) demand_id=2151",
            "[RelightAudit] job_submit job=809 column=(-5,3) source=boundary_dependency band=-64:319",
            "[RelightAudit] worker_done job=809 inputs_complete=0 unknown_inputs=23 compute_ms=47.2",
            "[RelightUnknownInput] job=809 coord=(-6,-4,4)",
            "[RelightAudit] calculation_certificate job=809 sections=8 inputs_complete=0",
            "[RelightAudit] slice_handoff job=809 coord=(-5,-3,3) settled_current=0 installed=1",
        ], "game.log.INFO.002": [
            "[WorldColumnSource] source=disk outcome=complete coord=(-6,0,4) demand_id=2151 elapsed_ms=17744 result_wait_max_ms=17673 file_read_ms=14.8",
            "[RelightAudit] worker_done job=818 inputs_complete=1 unknown_inputs=0",
            "[RelightAudit] calculation_certificate job=818 sections=8 inputs_complete=1",
            "[RelightAudit] slice_handoff job=818 coord=(-5,-3,3) settled_current=1 installed=0",
        ]}, [fault])
        section = report["sections"][0]
        self.assertEqual(section["depth_fault_attribution"], fault)
        self.assertFalse(section["trace_observed"])
        diagnostic = section["light_input_diagnostics"]
        self.assertEqual(diagnostic["retained_incomplete_input_jobs"], 1)
        self.assertEqual(diagnostic["maximum_reported_unknown_inputs"], 23)
        first = diagnostic["jobs"][0]
        self.assertEqual(first["calculation"]["certificate"]["sections"], 8)
        self.assertEqual(first["calculation"]["certificate"]["inputs_complete"], 0)
        load = first["calculation"]["reported_unknown_inputs"][0]["same_column_disk_load"]
        self.assertEqual(load["result_wait_max_ms"], 17673)
        self.assertEqual(load["demand_id"], 2151)
        self.assertNotIn("ready", first)
        self.assertNotIn("published", first)

    def test_missing_or_malformed_worker_metadata_never_becomes_complete(self):
        report = self.run_trace({"game.log.INFO.001": [
            "[RelightAudit] worker_done job=9 inputs_complete=2 unknown_inputs=-1",
            "[RelightAudit] slice_handoff job=9 coord=(-5,-3,3) settled_current=1",
            "[RelightAudit] slice_handoff job=10 coord=(-5,-3,3) settled_current=0",
        ]}, [dict(coord=[-5, -3, 3], faults=1)])
        self.assertEqual(report["malformed_light_input_rows"], 1)
        diagnostic = report["sections"][0]["light_input_diagnostics"]
        self.assertIsNone(diagnostic["maximum_reported_unknown_inputs"])
        self.assertTrue(all("worker" not in x["calculation"] for x in diagnostic["jobs"]))

    def test_data_priority_selection_does_not_mean_column_completion(self):
        report = self.run_trace({"game.log.INFO.001": [
            "[WorldColumnSource] source=disk outcome=queued coord=(-6,0,4) demand_id=7",
            "[WorldColumnSource] source=disk outcome=field_input_promoted coord=(-6,0,4) demand_id=7 slice_y=-4",
            "[RelightAudit] worker_done job=1 inputs_complete=0 unknown_inputs=1",
            "[RelightUnknownInput] job=1 coord=(-6,-4,4)",
            "[RelightAudit] slice_handoff job=1 coord=(-5,-3,3) settled_current=0",
        ]}, [dict(coord=[-5, -3, 3], faults=1)])
        load = report["sections"][0]["light_input_diagnostics"]["jobs"][0][
            "calculation"]["reported_unknown_inputs"][0]["same_column_disk_load"]
        self.assertEqual(load["promoted_slices"][0]["slice_y"], -4)
        self.assertNotIn("completed_at", load)

    def test_field_history_is_bounded_without_losing_handoff_count(self):
        lines = []
        for job in range(1, 301):
            lines.extend([
                f"[RelightAudit] worker_done job={job} inputs_complete=0 unknown_inputs=1",
                f"[RelightAudit] slice_handoff job={job} coord=(-5,-3,3) settled_current=0",
            ])
        report = self.run_trace({"game.log.INFO.001": lines}, [dict(coord=[-5, -3, 3], faults=1)])
        diagnostic = report["sections"][0]["light_input_diagnostics"]
        self.assertEqual(diagnostic["handoff_events"], 300)
        self.assertEqual(len(diagnostic["jobs"]), 64)
        self.assertEqual(diagnostic["unretained_handoff_events"], 236)
        self.assertEqual(diagnostic["jobs"][0]["calculation"]["worker"]["unknown_inputs"], 1)


if __name__ == "__main__":
    unittest.main()
