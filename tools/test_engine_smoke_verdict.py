"""Negative controls for native engine smoke acceptance."""
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

from engine_smoke_verdict import classify
import flight_sim_run


class EngineSmokeVerdictTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.perf = Path(self.temp.name) / "perf.jsonl"
        self.report = dict(exit_code=0, ingame_frames=100, start_focus=[0, 0],
                           end_focus=[20, 0], teleport_cruise=False,
                           diagnostic_relocations_after_start=0,
                           initial_resident_column_count=81,
                           outside_initial_resident_samples=100,
                           first_frame_required_ring_ready=True)

    def trace(self, **extra):
        self.perf.write_text("\n".join(json.dumps(dict(
            kind="renderer_pixel_probe", frame_epoch=i,
            visual_reference_checked=True, visual_fault=False, **extra))
            for i in range(3)), encoding="utf-8")

    def test_complete_evidence(self):
        self.trace()
        self.assertEqual(classify(self.report, self.perf)["status"], "PASS")

    def test_empty_trace_is_untested(self):
        self.perf.write_text("")
        self.assertEqual(classify(self.report, self.perf)["status"], "UNTESTED")

    def test_backend_execution_in_event_is_not_lost(self):
        rows = [dict(kind="period", cpu_light_seed_apply=0, gpu_light_seed_apply=0),
                dict(kind="visual_holes_enter", cpu_light_seed_apply=3,
                     gpu_light_seed_apply=0, gpu_light_readback=0)]
        self.perf.write_text("\n".join(json.dumps(row) for row in rows), encoding="utf-8")
        result = classify(self.report, self.perf)
        observed = result["backend_observations"]
        self.assertEqual(observed["maxima"]["cpu_light_seed_apply"], 3)
        self.assertEqual(observed["maxima"]["gpu_light_seed_apply"], 0)
        self.assertEqual(observed["positive_records"], {"cpu_light_seed_apply": 1})
        self.assertEqual(observed["records_by_kind"], {"period": 1, "visual_holes_enter": 1})
        self.assertEqual(result["status"], "UNTESTED")

    def test_missing_backend_counter_is_not_observed_zero(self):
        self.trace()
        observed = classify(self.report, self.perf)["backend_observations"]
        self.assertEqual(observed["maxima"], {})
        self.assertEqual(observed["positive_records"], {})

    def test_invalid_backend_counters_do_not_grant_execution(self):
        self.perf.write_text("\n".join(json.dumps(dict(
            kind="period", cpu_light_seed_apply=value))
            for value in [-1, True, 1.5, "3"]), encoding="utf-8")
        observed = classify(self.report, self.perf)["backend_observations"]
        self.assertEqual(observed["maxima"], {})
        self.assertEqual(observed["invalid_counter_rows"], 4)

    def test_legacy_pixels_do_not_grant_acceptance(self):
        self.perf.write_text('\n'.join(json.dumps(dict(kind="renderer_pixel_probe", frame_epoch=i)) for i in range(3)))
        self.assertEqual(classify(self.report, self.perf)["status"], "UNTESTED")

    def test_stalled_bot_fails_even_with_good_pixels(self):
        self.trace()
        self.report["end_focus"] = [0, 0]
        self.assertEqual(classify(self.report, self.perf)["components"]["route"]["status"], "FAIL")

    def test_travel_inside_initial_loaded_footprint_fails(self):
        self.trace()
        self.report["outside_initial_resident_samples"] = 0
        self.assertEqual(classify(self.report, self.perf)["components"]["route"]["status"], "FAIL")

    def test_unrecorded_height_correction_is_untested(self):
        self.trace()
        del self.report["diagnostic_relocations_after_start"]
        self.assertEqual(classify(self.report, self.perf)["components"]["route"]["status"], "UNTESTED")

    def test_teleport_fails(self):
        self.trace()
        self.report["teleport_cruise"] = True
        self.assertEqual(classify(self.report, self.perf)["status"], "FAIL")

    def test_joined_fault_fails(self):
        self.perf.write_text(json.dumps(dict(kind="renderer_pixel_probe", frame_epoch=1,
                                             visual_reference_checked=True, visual_fault=True)))
        self.assertEqual(classify(self.report, self.perf)["status"], "FAIL")

    def test_malformed_trace_does_not_pass(self):
        self.trace()
        with self.perf.open("a") as stream:
            stream.write("\n{truncated")
        self.assertEqual(classify(self.report, self.perf)["status"], "UNTESTED")

    def test_first_frame_debt_fails(self):
        self.trace()
        self.report["first_frame_required_ring_ready"] = False
        self.assertEqual(classify(self.report, self.perf)["status"], "FAIL")

    def test_missing_fault_result_is_untested(self):
        self.perf.write_text('\n'.join(json.dumps(dict(kind="renderer_pixel_probe", frame_epoch=i,
                                                     visual_reference_checked=True)) for i in range(3)))
        self.assertEqual(classify(self.report, self.perf)["status"], "UNTESTED")

    def test_boolean_relocation_count_is_not_evidence(self):
        self.trace()
        self.report["diagnostic_relocations_after_start"] = False
        self.assertEqual(classify(self.report, self.perf)["status"], "UNTESTED")

    def test_endpoint_travel_does_not_hide_incomplete_bot_route(self):
        self.trace()
        self.report["diagnostic_bot"] = dict(enabled=True, state="inspect", reason="")
        result = classify(self.report, self.perf)
        self.assertEqual(result["status"], "FAIL")
        self.assertEqual(result["components"]["route"]["reason"], "bot_route_incomplete")

    def test_queued_captures_do_not_replace_written_pose_evidence(self):
        self.trace()
        self.report["diagnostic_bot"] = dict(enabled=True, state="complete",
                                            inspection_capture_requested_n=12,
                                            inspection_capture_queued_n=12)
        self.report["inspection_capture_files_verified_n"] = 11
        result = classify(self.report, self.perf)
        self.assertEqual(result["status"], "FAIL")
        self.assertEqual(result["components"]["inspection"]["verified"], 11)


class FlightProcessOwnershipTest(unittest.TestCase):
    def test_cleanup_targets_only_live_owned_child(self):
        live, exited = Mock(), Mock()
        live.poll.return_value = None
        exited.poll.return_value = 0
        with patch.dict(flight_sim_run._owned_flight_processes,
                        {123: live, 456: exited}, clear=True):
            with patch.object(flight_sim_run, "kill_process_tree") as kill:
                self.assertEqual(flight_sim_run.kill_cubatarium_orphans(), 1)
                kill.assert_called_once_with(123)
                self.assertNotIn(456, flight_sim_run._owned_flight_processes)

    def test_cleanup_without_owned_children_never_runs_taskkill(self):
        with patch.dict(flight_sim_run._owned_flight_processes, {}, clear=True):
            with patch.object(flight_sim_run.subprocess, "run") as run:
                self.assertEqual(flight_sim_run.kill_cubatarium_orphans(), 0)
                run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
