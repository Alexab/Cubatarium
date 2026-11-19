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
        fault = dict(coord=[-5, -3, 3], faults=78, no_published_geometry=78)
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


if __name__ == "__main__":
    unittest.main()
