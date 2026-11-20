"""Fixture isolation must survive failed/interrupted diagnostic runs."""
import tempfile
import unittest
import json
import struct
import zlib
from pathlib import Path
from unittest.mock import Mock, patch

from engine_smoke_run import isolated_fixture, world_fingerprint
from engine_smoke_run import valid_png, inspection_evidence
import engine_smoke_run


class IsolatedFixtureTest(unittest.TestCase):
    def test_read_probe_requires_column_boolean_and_no_cancel_probe(self):
        for options in (dict(probe_disk_read_failure=True),dict(probe_disk_read_failure=1),
                        dict(probe_disk_read_failure=True,secondary_residency_column=(1,5),probe_partial_air_cancellation=True)):
            with self.assertRaisesRegex(ValueError,"disk read probe"):
                engine_smoke_run.run("Saved",Path("unused"),**options)
    def test_cancellation_probe_requires_explicit_column_and_boolean(self):
        for value in (True, 1, "1", None):
            with self.assertRaisesRegex(ValueError, "AIR cancellation probe"):
                engine_smoke_run.run("Saved", Path("unused"),
                                     probe_partial_air_cancellation=value)
    def test_secondary_residency_requires_explicit_valid_moving_column(self):
        for column, options in [(True, {}), ([1], {}), ([1, 2, 3], {}),
                                ([True, 2], {}), ([1.0, 2], {}),
                                ([2**31, 2], {}), ([1, 2], {"round_trip": False}),
                                ([1, 2], {"startup_only": True})]:
            with self.subTest(column=column, options=options):
                with self.assertRaisesRegex(ValueError, "secondary residency"):
                    engine_smoke_run.run("Saved", Path("unused"),
                                         **({"round_trip": True} | options),
                                         secondary_residency_column=column)

    def test_secondary_trace_reaches_child_and_failure_preserves_source(self):
        import os
        (self.runtime / "Cubatarium.exe").write_bytes(b"test executable identity")
        fingerprint = world_fingerprint(self.world)
        for index, (column, probe, read_probe) in enumerate(((None, False,False), ((1, 5), False,False), ((1, 5), True,False), ((1,5),False,True))):
            artifacts = self.artifacts / str(index)
            def fail_launch(*args, **kwargs):
                env = kwargs["env"]
                if read_probe:
                    self.assertEqual(env["CUBA_DISK_READ_FAILURE_PROBE"],"1,5")
                    self.assertEqual(Path(env["CUBA_DISK_READ_FAILURE_ARTIFACTS"]),(artifacts/"disk-read-save").resolve())
                else:
                    self.assertNotIn("CUBA_DISK_READ_FAILURE_PROBE",env)
                    self.assertNotIn("CUBA_DISK_READ_FAILURE_ARTIFACTS",env)
                if probe:
                    self.assertEqual(env["CUBA_DISK_AIR_CANCEL_PROBE"], "1,5")
                else:
                    self.assertNotIn("CUBA_DISK_AIR_CANCEL_PROBE", env)
                if column is None:
                    self.assertNotIn("CUBA_CHUNK_RESIDENCY_TRACE_SECONDARY", env)
                    self.assertNotIn("CUBA_CHUNK_RESIDENCY_TRACE_SECONDARY_FILE", env)
                else:
                    self.assertEqual(env["CUBA_CHUNK_RESIDENCY_TRACE_SECONDARY"], "1,5")
                    self.assertEqual(Path(env["CUBA_CHUNK_RESIDENCY_TRACE_SECONDARY_FILE"]),
                                     (artifacts / "secondary-column-residency.log").resolve())
                    self.assertNotEqual(env["CUBA_CHUNK_RESIDENCY_TRACE_SECONDARY_FILE"],
                                        env["CUBA_CHUNK_RESIDENCY_TRACE_FILE"])
                raise RuntimeError("controlled secondary launch failure")
            with patch.object(engine_smoke_run, "ROOT", self.root), \
                 patch.dict(os.environ, {"CUBA_CHUNK_RESIDENCY_TRACE_SECONDARY": "99,99",
                                         "CUBA_DISK_AIR_CANCEL_PROBE": "99,99",
                                         "CUBA_DISK_READ_FAILURE_PROBE": "99,99",
                                         "CUBA_DISK_READ_FAILURE_ARTIFACTS": "foreign",
                                         "CUBA_CHUNK_RESIDENCY_TRACE_SECONDARY_FILE": "foreign.log"}), \
                 patch("flight_sim_run.exe_writable", return_value=True), \
                 patch.object(engine_smoke_run.subprocess, "check_output", return_value="test-sha"), \
                 patch.object(engine_smoke_run.subprocess, "Popen", side_effect=fail_launch):
                with self.assertRaisesRegex(RuntimeError, "controlled secondary launch failure"):
                    engine_smoke_run.run("Saved", artifacts, round_trip=True,
                                         secondary_residency_column=column,
                                         probe_partial_air_cancellation=probe,
                                         probe_disk_read_failure=read_probe)
            self.assertEqual(self.config.read_bytes(), self.original)
            self.assertEqual(world_fingerprint(self.world), fingerprint)
            self.assertFalse((self.runtime / ".engine-smoke.lock").exists())

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.runtime = self.root / "bin"
        self.world = self.runtime / "worlds" / "Saved"
        self.world.mkdir(parents=True)
        (self.world / "world_data.json").write_text('{"world_seed":42}')
        (self.world / "chunks").mkdir()
        (self.world / "chunks" / "0_0_0.cchunk").write_bytes(b"fixture chunk")
        self.config = self.runtime / "config.json"
        self.original = b'{"original": true}\r\n'
        self.config.write_bytes(self.original)
        self.artifacts = self.root / "artifacts"

    def test_render_distance_scenario_restores_configuration_on_failure(self):
        with self.assertRaisesRegex(RuntimeError, "after override"):
            with isolated_fixture(self.runtime, "Saved", self.artifacts):
                self.config.write_bytes(engine_smoke_run.render_distance_payload(self.config.read_bytes(), 8))
                config = json.loads(self.config.read_bytes())
                self.assertEqual(config, {"original": True, "render_distance_chunks": 8})
                raise RuntimeError("after override")
        self.assertEqual(self.config.read_bytes(), self.original)
        for invalid in (0, 33, True, 4.5, "8"):
            with self.assertRaises(ValueError):
                engine_smoke_run.render_distance_payload(self.original, invalid)

    def test_failure_restores_config_and_preserves_original_world(self):
        fingerprint = world_fingerprint(self.world)
        fixture = None
        with self.assertRaisesRegex(RuntimeError, "simulated"):
            with isolated_fixture(self.runtime, "Saved", self.artifacts) as (fixture, source_hash):
                self.assertEqual(source_hash, fingerprint)
                self.assertEqual(world_fingerprint(fixture), fingerprint)
                self.config.write_bytes(b"changed by app")
                (fixture / "chunks" / "new.cchunk").write_bytes(b"generated")
                raise RuntimeError("simulated child failure")
        self.assertEqual(world_fingerprint(self.world), fingerprint)
        self.assertEqual(self.config.read_bytes(), self.original)
        self.assertFalse((self.runtime / ".engine-smoke.lock").exists())
        self.assertTrue((fixture / "chunks" / "new.cchunk").exists())
        self.assertEqual((self.artifacts / "config.original.json").read_bytes(), self.original)

    def test_synchronous_meshing_restores_configuration_on_failure(self):
        original = b'{"render":{"gpu_packed_meshing":true,"async_meshing":true},"other":7}\r\n'
        self.config.write_bytes(original)
        with self.assertRaisesRegex(RuntimeError, "after sync override"):
            with isolated_fixture(self.runtime, "Saved", self.artifacts):
                self.config.write_bytes(engine_smoke_run.synchronous_meshing_payload(self.config.read_bytes()))
                config = json.loads(self.config.read_bytes())
                self.assertIs(config["render"]["async_meshing"], False)
                self.assertIs(config["render"]["gpu_packed_meshing"], True)
                self.assertEqual(config["other"], 7)
                raise RuntimeError("after sync override")
        self.assertEqual(self.config.read_bytes(), original)

    def test_synchronous_meshing_requires_actual_runtime_observations(self):
        path = self.root / "mode.json"
        self.assertEqual(engine_smoke_run.synchronous_meshing_evidence(path)["status"], "UNTESTED")
        for samples, expected in (([], "UNTESTED"),
                                  ([{"async_meshing_enabled": False}] * 3, "PASS"),
                                  ([{"async_meshing_enabled": False}, {"async_meshing_enabled": True}], "FAIL"),
                                  ([{"async_meshing_enabled": 0}], "FAIL"),
                                  ([{}], "FAIL")):
            with self.subTest(samples=samples):
                path.write_text(json.dumps({"samples": samples}), encoding="utf-8")
                self.assertEqual(engine_smoke_run.synchronous_meshing_evidence(path)["status"], expected)

    def test_unpacked_meshing_request_reaches_child_and_restores_config_on_failure(self):
        original=b'{"render":{"gpu_packed_meshing":true,"async_meshing":true},"other":7}\r\n'
        self.config.write_bytes(original)
        (self.runtime/'Cubatarium.exe').write_bytes(b'test executable identity')
        fingerprint=world_fingerprint(self.world)
        def fail_launch(*args,**kwargs):
            config=json.loads(self.config.read_bytes())
            self.assertIs(config['render']['gpu_packed_meshing'],False)
            self.assertIs(config['render']['async_meshing'],True)
            self.assertEqual(config['other'],7)
            raise RuntimeError('controlled child launch failure')
        with patch.object(engine_smoke_run,'ROOT',self.root), \
             patch('flight_sim_run.exe_writable',return_value=True), \
             patch.object(engine_smoke_run.subprocess,'check_output',return_value='test-sha'), \
             patch.object(engine_smoke_run.subprocess,'Popen',side_effect=fail_launch):
            with self.assertRaisesRegex(RuntimeError,'controlled child launch failure'):
                engine_smoke_run.run('Saved',self.artifacts,unpacked_meshing=True)
        self.assertEqual(self.config.read_bytes(),original)
        self.assertEqual(world_fingerprint(self.world),fingerprint)
        manifest=json.loads((self.artifacts/'route.json').read_text(encoding='utf-8'))
        self.assertIs(manifest['unpacked_meshing_requested'],True)
        self.assertEqual((self.artifacts/'config.unpacked-meshing.json').read_bytes(),
                         engine_smoke_run.unpacked_meshing_payload(original))
        self.assertFalse((self.runtime/'.engine-smoke.lock').exists())
        with self.assertRaises(ValueError):
            engine_smoke_run.run('Saved',self.root/'invalid',unpacked_meshing=1)

    def test_second_runner_cannot_take_live_lease(self):
        with isolated_fixture(self.runtime, "Saved", self.artifacts):
            with self.assertRaises(FileExistsError):
                with isolated_fixture(self.runtime, "Saved", self.root / "second"):
                    self.fail("another runner acquired a live lease")
            self.assertTrue((self.runtime / ".engine-smoke.lock").exists())

    def test_artifacts_cannot_contaminate_source_save(self):
        with self.assertRaises(ValueError):
            with isolated_fixture(self.runtime, "Saved", self.world / "artifacts"):
                self.fail("nested output accepted")

    def test_traversal_rejected(self):
        for name in ["../Saved", "..", "Saved/sub", "Saved\\sub", "X:Saved"]:
            with self.subTest(name=name), self.assertRaises(ValueError):
                with isolated_fixture(self.runtime, name, self.artifacts):
                    self.fail("unsafe world name accepted")

    def test_existing_artifacts_are_never_overwritten(self):
        self.artifacts.mkdir()
        with self.assertRaises(FileExistsError):
            with isolated_fixture(self.runtime, "Saved", self.artifacts):
                self.fail("reused artifact directory")

    def test_interrupt_stops_owned_runner_before_restoring_config(self):
        (self.runtime / "Cubatarium.exe").write_bytes(b"test executable identity")
        child = Mock(pid=123)
        def wait():
            self.config.write_bytes(b"child changed settings")
            raise KeyboardInterrupt()
        calls = 0
        def interrupt_once(*args, **kwargs):
            nonlocal calls
            calls += 1
            if calls == 1:
                wait()
            return 0
        child.wait.side_effect = interrupt_once
        with patch.object(engine_smoke_run, "ROOT", self.root), \
             patch("flight_sim_run.exe_writable", return_value=True), \
             patch.object(engine_smoke_run.subprocess, "check_output", return_value="test-sha"), \
             patch.object(engine_smoke_run.subprocess, "Popen", return_value=child), \
             patch("flight_sim_run.kill_process_tree") as kill:
            with self.assertRaises(KeyboardInterrupt):
                engine_smoke_run.run("Saved", self.artifacts)
            kill.assert_called_once_with(123)
        self.assertEqual(self.config.read_bytes(), self.original)
        self.assertFalse((self.runtime / ".engine-smoke.lock").exists())

    def test_round_trip_constructs_return_and_requires_lifecycle_evidence(self):
        (self.runtime / "Cubatarium.exe").write_bytes(b"test executable identity")
        child = Mock(pid=123, returncode=0)
        with patch.object(engine_smoke_run, "ROOT", self.root), \
             patch("flight_sim_run.exe_writable", return_value=True), \
             patch.object(engine_smoke_run.subprocess, "check_output", return_value="test-sha"), \
             patch.object(engine_smoke_run.subprocess, "Popen", return_value=child):
            verdict = engine_smoke_run.run("Saved", self.artifacts, round_trip=True)
        route = json.loads((self.artifacts / "bot-route.json").read_text(encoding="utf-8"))
        self.assertEqual(route["purpose"], "round_trip")
        self.assertEqual(len(route["waypoints"]), 3)
        self.assertEqual(route["waypoints"][0], route["waypoints"][2])
        manifest = json.loads((self.artifacts / "route.json").read_text(encoding="utf-8"))
        self.assertTrue(manifest["round_trip"])
        self.assertIn("--no-hold-forward", manifest["command"])
        self.assertEqual(manifest["diagnostic_environment"]["CUBA_CHUNK_RESIDENCY_TRACE_START_COLUMN"], "1")
        self.assertNotEqual(verdict["status"], "PASS")
        self.assertEqual(self.config.read_bytes(), self.original)
        with self.assertRaises(ValueError):
            engine_smoke_run.run("Saved", self.artifacts, round_trip=True, startup_only=True)

    def test_bound_observation_is_explicit_and_never_inherits_acceptance(self):
        (self.runtime / "Cubatarium.exe").write_bytes(b"test executable identity")
        fingerprint = world_fingerprint(self.world)
        for enabled in (False, True):
            artifacts = self.root / ("bound" if enabled else "ordinary")
            child = Mock(pid=123, returncode=0)
            with patch.object(engine_smoke_run, "ROOT", self.root), \
                 patch("flight_sim_run.exe_writable", return_value=True), \
                 patch.object(engine_smoke_run.subprocess, "check_output", return_value="test-sha"), \
                 patch.dict(engine_smoke_run.os.environ, {"CUBA_RGB_REFERENCE": "1"}), \
                 patch.object(engine_smoke_run.subprocess, "Popen", return_value=child) as launch:
                verdict = engine_smoke_run.run("Saved", artifacts, startup_only=True,
                                               observe_bound_light=enabled)
            manifest = json.loads((artifacts / "route.json").read_text(encoding="utf-8"))
            self.assertIs(manifest["observe_bound_light"], enabled)
            self.assertIsNone(manifest["fixture_scene"])
            self.assertEqual(launch.call_args.kwargs["env"].get("CUBA_RGB_REFERENCE"),
                             "1" if enabled else None)
            self.assertNotEqual(verdict["status"], "PASS")
            self.assertEqual(world_fingerprint(self.world), fingerprint)
            self.assertEqual(self.config.read_bytes(), self.original)
        with self.assertRaises(ValueError):
            engine_smoke_run.run("Saved", self.artifacts, observe_bound_light=1)

    def test_startup_runner_records_scope_and_native_sprint_but_requires_report(self):
        (self.runtime / "Cubatarium.exe").write_bytes(b"test executable identity")
        child = Mock(pid=123, returncode=0)
        with patch.object(engine_smoke_run, "ROOT", self.root), \
             patch("flight_sim_run.exe_writable", return_value=True), \
             patch.object(engine_smoke_run.subprocess, "check_output", return_value="test-sha"), \
             patch.object(engine_smoke_run.subprocess, "Popen", return_value=child):
            verdict = engine_smoke_run.run("Saved", self.artifacts, startup_only=True, sprint=True)
        self.assertEqual(verdict["schema"], "engine-startup-smoke-verdict.v1")
        self.assertEqual(verdict["status"], "FAIL")
        self.assertEqual(verdict["components"]["runtime"]["reason"], "missing_native_report")
        route = json.loads((self.artifacts / "bot-route.json").read_text(encoding="utf-8"))
        self.assertEqual(route["waypoints"], [])
        self.assertEqual(route["purpose"], "startup_observation")
        manifest = json.loads((self.artifacts / "route.json").read_text(encoding="utf-8"))
        self.assertTrue(manifest["startup_only"])
        self.assertTrue(manifest["sprint"])
        self.assertIn("--sprint", manifest["command"])
        self.assertIn("--no-hold-forward", manifest["command"])
        self.assertEqual(self.config.read_bytes(), self.original)

    def test_source_inspection_retains_dense_probes_and_original_saved_world(self):
        import test_engine_source_observation as observations
        fixture=observations.SourceObservationTest();fixture.setUp()
        scene=fixture.scene
        (self.runtime/'Cubatarium.exe').write_bytes(b'test executable identity')
        (self.world/'users.json').write_text(json.dumps({'Username':{'position':scene['starting_eye']}}),encoding='utf-8')
        fingerprint=world_fingerprint(self.world)
        plan=dict(schema='engine-native-world-actions.v1',**fixture.plan)
        child=Mock(pid=123,returncode=0)
        with patch.object(engine_smoke_run,'ROOT',self.root), \
             patch('flight_sim_run.exe_writable',return_value=True), \
             patch.object(engine_smoke_run.subprocess,'check_output',return_value='test-sha'), \
             patch.object(engine_smoke_run.subprocess,'Popen',return_value=child) as launch:
            verdict=engine_smoke_run.run('Saved',self.artifacts,round_trip=True,
                observe_bound_light=True,world_actions=plan,source_observation_scene=scene)
        route=json.loads((self.artifacts/'bot-route.json').read_text(encoding='utf-8'))
        self.assertEqual(route['waypoints'][0]['position'],[0,2,0])
        self.assertEqual(len(route['waypoints'][0]['inspection_targets']),8)
        self.assertEqual(route['world_actions']['after_waypoints'],1)
        env=launch.call_args.kwargs['env']
        self.assertEqual(env['CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS'],'1')
        self.assertEqual(env['CUBA_RGB_REFERENCE'],'1')
        self.assertNotIn('CUBA_VISUAL_BLACK_TRACE_CENTER_EVERY_FRAME',env)
        self.assertEqual(world_fingerprint(self.world),fingerprint)
        self.assertEqual(self.config.read_bytes(),self.original)
        self.assertNotEqual(verdict['status'],'PASS')

    def test_open_plane_enables_normal_viewmodel_without_qa_injection(self):
        (self.runtime / "Cubatarium.exe").write_bytes(b"test executable identity")
        child = Mock(pid=123, returncode=0)
        scene = dict(name="open-sky-plane", starting_eye=[22, -58.88, 51],
                     plane_bounds=dict(floor=-60.5), route_rise=20)
        with patch.object(engine_smoke_run, "ROOT", self.root), \
             patch("flight_sim_run.exe_writable", return_value=True), \
             patch.object(engine_smoke_run.subprocess, "check_output", return_value="test-sha"), \
             patch.object(engine_smoke_run.subprocess, "Popen", return_value=child), \
             patch("engine_smoke_fixtures.prepare_open_sky_plane", return_value=scene):
            engine_smoke_run.run("Saved", self.artifacts, fixture_scene="open-sky-plane",
                                 capture_encode_delay_ms=2500)
        manifest = json.loads((self.artifacts / "route.json").read_text(encoding="utf-8"))
        env = manifest["diagnostic_environment"]
        self.assertEqual(env["CUBA_FLIGHT_BENCH_OVERLAY"], "0")
        self.assertEqual(env["CUBA_FLIGHT_CAPTURE_ENCODE_DELAY_MS"], "2500")
        self.assertEqual(manifest["capture_encode_delay_ms"], 2500)
        self.assertNotIn("CUBA_FLIGHT_QA_FP_ITEM", env)
        self.assertNotIn("CUBA_FLIGHT_QA_WORLD_USER", env)
        self.assertEqual(self.config.read_bytes(), self.original)

    def test_encoder_delay_validation_precedes_process_or_fixture_changes(self):
        for invalid in (-1, 10001, True, "2500", 2.5, float("nan")):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                engine_smoke_run.run("Saved", self.artifacts,
                                     capture_encode_delay_ms=invalid)
        self.assertFalse(self.artifacts.exists())
        self.assertEqual(self.config.read_bytes(), self.original)

    def test_backpressure_requires_saturation_and_lossless_stationary_capture(self):
        logs = self.artifacts / "logs"
        logs.mkdir(parents=True)
        log = logs / "app.log.INFO.1"
        log.write_text("")
        trace = self.artifacts / "diagnostic_bot.jsonl"
        row = dict(state="inspect", capture_ready=0, capture_request=0,
                   forward=0, up=0, down=0)
        trace.write_text(json.dumps(row)+"\n")
        complete = dict(verified=2, requested=2)
        check = lambda: engine_smoke_run.capture_backpressure_evidence(self.artifacts, complete)
        self.assertEqual(check()["status"], "UNTESTED")
        log.write_text("[FlightCapture] encoder_backpressure retained=1\n")
        self.assertEqual(check()["status"], "PASS")
        trace.write_text(json.dumps(dict(row, forward=1))+"\n")
        self.assertEqual(check()["status"], "FAIL")
        trace.write_text(json.dumps(dict(row, capture_request=1))+"\n")
        self.assertEqual(check()["status"], "FAIL")
        trace.write_text(json.dumps(row)+"\n")
        log.write_text("[FlightCapture] encoder_backpressure retained=1\n"
                       "[FlightCapture] PNG worker queue full; dropped completed frame index=4\n")
        self.assertEqual(check()["status"], "FAIL")
        log.write_text("[FlightCapture] encoder_backpressure retained=1\n")
        complete["verified"] = 1
        self.assertEqual(check()["status"], "FAIL")

    def test_startup_observation_checks_native_inputs_and_three_dimensional_drift(self):
        trace = self.root / "startup.jsonl"
        observe = engine_smoke_run.startup_observation_evidence
        self.assertEqual(observe(trace)["status"], "UNTESTED")
        row = dict(schema="diagnostic-bot-trace.v1", position=[1, -40, 3], forward=0, up=0, down=0, time=0)
        def write(rows):
            trace.write_text("".join(json.dumps(r) + "\n" for r in rows), encoding="utf-8")
        write([row])
        self.assertEqual(observe(trace)["status"], "UNTESTED")
        write([row, dict(row, time=1)])
        self.assertEqual(observe(trace)["status"], "PASS")
        write([row, row])
        self.assertEqual(observe(trace)["status"], "FAIL")
        write([row, dict(row, up=1, time=1)])
        self.assertEqual(observe(trace)["reason"], "startup_observation_movement_input")
        write([row, dict(row, position=[1, -39, 3], time=1)])
        self.assertEqual(observe(trace)["reason"], "startup_observation_position_changed")
        write([row, dict(row, position=[1, float("nan"), 3], time=1)])
        self.assertEqual(observe(trace)["status"], "FAIL")

    def png(self):
        def chunk(kind, data):
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
        return (b"\x89PNG\r\n\x1a\n" +
                chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(b"\0\0\0\0\xff")) + chunk(b"IEND", b""))

    def test_truncated_and_corrupt_png_are_not_evidence(self):
        path = self.root / "capture.png"
        payload = self.png()
        path.write_bytes(payload)
        self.assertTrue(valid_png(path))
        path.write_bytes(payload[:-3])
        self.assertFalse(valid_png(path))
        corrupt = bytearray(payload)
        corrupt[20] ^= 1
        path.write_bytes(corrupt)
        self.assertFalse(valid_png(path))

    def test_inspection_requires_distinct_viewpoints_and_written_images(self):
        (self.artifacts / "captures").mkdir(parents=True)
        rows = []
        for i in range(12):
            rows.append(dict(capture_request=1, capture_id=i, yaw=90 * (i // 3),
                             pitch=-45 + 45 * (i % 3)))
            (self.artifacts / "captures" / f"inspection_{i:04}.png").write_bytes(self.png())
        trace = self.artifacts / "diagnostic_bot.jsonl"
        trace.write_text("\n".join(json.dumps(row) for row in rows))
        report = dict(diagnostic_bot=dict(inspection_capture_requested_n=12))
        self.assertEqual(inspection_evidence(self.artifacts, report)["verified"], 12)
        (self.artifacts / "captures" / "inspection_0005.png").unlink()
        self.assertEqual(inspection_evidence(self.artifacts, report)["verified"], 11)
        for row in rows:
            row["yaw"] = 0
        trace.write_text("\n".join(json.dumps(row) for row in rows))
        self.assertLess(inspection_evidence(self.artifacts, report)["verified"], 12)


    def test_inspection_joins_variable_route_layout_and_relative_targets(self):
        import math
        (self.artifacts / "captures").mkdir(parents=True)
        route = dict(schema="diagnostic-flight-route.v1", relative=True, waypoints=[
            dict(position=[0, 0, 0], inspection_targets=[[2, -7.5, 0]]),
            dict(position=[-16, 0, 0], inspection_targets=[[-14, -7.5, 0]])])
        route_path = self.artifacts / "bot-route.json"
        route_path.write_text(json.dumps(route))
        rows = []
        for waypoint in range(2):
            for local in range(13):
                capture_id = len(rows)
                rows.append(dict(capture_request=1, capture_id=capture_id,
                    position=[22 - 16 * waypoint, -36, 51], state="inspect",
                    waypoints_visited=waypoint, forward=0, up=0, down=0,
                    yaw=90 * (local // 3) if local < 12 else 0,
                    pitch=-45 + 45 * (local % 3) if local < 12 else math.degrees(math.atan2(-7.5, 2))))
                (self.artifacts / "captures" / f"inspection_{capture_id:04}.png").write_bytes(self.png())
        trace = self.artifacts / "diagnostic_bot.jsonl"
        def write():
            trace.write_text("\n".join(json.dumps(row) for row in rows))
        write()
        report = dict(diagnostic_bot=dict(inspection_capture_requested_n=26))
        self.assertEqual(inspection_evidence(self.artifacts, report)["verified"], 26)
        rows[12]["pitch"] = 0
        write()
        self.assertEqual(inspection_evidence(self.artifacts, report)["verified"], 25)
        rows[25]["forward"] = 1
        write()
        self.assertEqual(inspection_evidence(self.artifacts, report)["verified"], 24)
        report["diagnostic_bot"]["inspection_capture_requested_n"] = 25
        self.assertEqual(inspection_evidence(self.artifacts, report)["reason"], "invalid_inspection_id")
        report["diagnostic_bot"]["inspection_capture_requested_n"] = 27
        self.assertEqual(inspection_evidence(self.artifacts, report)["reason"], "inspection_route_capture_count_mismatch")
        route["waypoints"][0]["inspection_targets"][0][1] = float("nan")
        route_path.write_text(json.dumps(route))
        self.assertEqual(inspection_evidence(self.artifacts, report)["reason"], "invalid_inspection_route_evidence")


class NegativeCaveFixtureTest(unittest.TestCase):
    def test_scene_is_prepared_only_in_clone_and_matches_analytic_geometry(self):
        from engine_smoke_fixtures import prepare_dry_negative_cave, prepare_negative_cave
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            runtime = root / "bin"
            source = runtime / "worlds" / "Saved"
            (source / "chunks").mkdir(parents=True)
            (runtime / "config.json").write_text("{}")
            (source / "world_data.json").write_text(json.dumps({
                "procedural": {"min_height": -64, "max_height": 319}}))
            (source / "users.json").write_text(json.dumps({"Username": {"position": [1, 65, 1]}}))
            (source / "chunks" / "0_-3_3.cchunk").write_bytes(b"original")
            before = world_fingerprint(source)
            with isolated_fixture(runtime, "Saved", root / "evidence") as (fixture, _):
                scene = prepare_dry_negative_cave(fixture, 1)
                self.assertFalse((fixture / "chunks" / "0_-3_3.cchunk").exists())
                payload = json.loads((fixture / "chunks" / "0_-3_3.json").read_text())
                voxels = {(v["lx"], v["ly"] - 48, v["lz"] + 48): v["type"]
                          for v in payload["voxels"]}
                self.assertEqual(voxels[(8, -44, 51)], "stone")
                self.assertNotIn((8, -40, 51), voxels)
                self.assertEqual(scene["starting_eye"][1], -41.88)
                ocean = json.loads((fixture / "chunks" / "0_3_3.json").read_text())
                self.assertTrue(any(v["type"] == "water" and v["ly"] == 15 for v in ocean["voxels"]))
                wet = prepare_negative_cave(fixture, 1, flooded=True)
                flooded = json.loads((fixture / "chunks" / "0_-3_3.json").read_text())
                self.assertTrue(wet["expected_eye_in_fluid"])
                self.assertTrue(any(v["lx"] == 8 and v["ly"] == 8 and v["lz"] == 3
                                    and v["type"] == "water" for v in flooded["voxels"]))
                wide = prepare_negative_cave(fixture, 1, wide=True)
                self.assertGreater(wide["omitted_internal_air_slices"], 0)
                self.assertFalse((fixture / "chunks" / "0_-3_3.json").exists())
                self.assertFalse((fixture / "chunks" / "0_-2_3.json").exists())
                self.assertTrue((fixture / "chunks" / "0_-4_3.json").exists())
                self.assertTrue((fixture / "chunks" / "0_-1_3.json").exists())
                self.assertEqual(wide["route_rise"], 30)
                from engine_smoke_fixtures import prepare_lit_negative_cave
                metadata_path = fixture / "world_data.json"
                metadata = json.loads(metadata_path.read_text())
                metadata["resource_packs"] = {"primary": ["cubatarium_worldgen_cc0"]}
                metadata_path.write_text(json.dumps(metadata))
                material_path = runtime / "resource_packs/cubatarium_worldgen_cc0/blocks/glowstone.json"
                material_path.parent.mkdir(parents=True)
                material = dict(name="glowstone", id=376, lighting=dict(emission=15))
                material_path.write_text(json.dumps(material))
                lit = prepare_lit_negative_cave(fixture, 1)
                self.assertGreater(len(lit["lighting_fixture"]["emitters"]), 4)
                floor = json.loads((fixture / "chunks/0_-3_3.json").read_text())
                self.assertTrue(any(v["type"] == "glowstone" and v["lx"] == 0 and
                                    v["ly"] == 4 and v["lz"] == 3 for v in floor["voxels"]))
                prepared = world_fingerprint(fixture)
                material["lighting"]["emission"] = 0
                material_path.write_text(json.dumps(material))
                with self.assertRaises(ValueError):
                    prepare_lit_negative_cave(fixture, 1)
                self.assertEqual(world_fingerprint(fixture), prepared)
                from engine_smoke_fixtures import prepare_open_sky_plane
                (material_path.parent / "stone.json").write_text(json.dumps(dict(name="stone", id=560)))
                (fixture / "chunks/0_18_3.cchunk").write_bytes(b"high saved geometry")
                plane = prepare_open_sky_plane(fixture, 1)
                self.assertEqual(plane["name"], "open-sky-plane")
                self.assertTrue(plane["required_air_light_service"])
                self.assertEqual(plane["route_rise"], 20)
                self.assertFalse((fixture / "chunks/0_18_3.cchunk").exists())
                self.assertFalse((fixture / "chunks/0_-1_3.json").exists())
                self.assertFalse((fixture / "chunks/0_-3_3.json").exists())
                floor = json.loads((fixture / "chunks/0_-4_3.json").read_text())
                self.assertEqual({v["ly"] for v in floor["voxels"]}, {0, 1, 2, 3})
                self.assertEqual({v["type"] for v in floor["voxels"]}, {"stone"})
                self.assertEqual(plane["lighting_fixture"]["sky_level"], 15)
            self.assertEqual(world_fingerprint(source), before)
            with self.assertRaises(ValueError):
                prepare_dry_negative_cave(source, 1)

    def test_trace_rejects_recovery_teleport_wet_classification_and_missing_witness(self):
        from engine_smoke_fixtures import evaluate_fixture_trace
        with tempfile.TemporaryDirectory() as temp:
            trace = Path(temp) / "trace.jsonl"
            scene = {"starting_eye": [22, -41.88, 51], "expected_max_eye_y": -30.5,
                     "expected_eye_in_fluid": False}
            rows = [{"schema": "diagnostic-bot-trace.v1", "position": [22, -41.88, 51],
                     "eye_in_fluid": False},
                    {"schema": "diagnostic-bot-trace.v1", "position": [-40, -36, 51],
                     "eye_in_fluid": False}]
            def check():
                trace.write_text("\n".join(json.dumps(r) for r in rows))
                return evaluate_fixture_trace(scene, trace)
            self.assertEqual(check()["status"], "PASS")
            scene["expected_eye_in_fluid"] = True
            self.assertEqual(check()["reason"], "wet_cave_classified_as_air")
            for row in rows:
                row["eye_in_fluid"] = True
            self.assertEqual(check()["reason"], "negative_wet_cave_observed")
            scene["expected_eye_in_fluid"] = False
            for row in rows:
                row["eye_in_fluid"] = False
            rows[1]["eye_in_fluid"] = True
            self.assertEqual(check()["reason"], "dry_cave_classified_as_fluid")
            del rows[1]["eye_in_fluid"]
            self.assertEqual(check()["status"], "UNTESTED")
            rows[1]["position"][1] = 68
            self.assertEqual(check()["reason"], "camera_left_negative_cave")
            rows[0]["position"][0] = 25
            self.assertEqual(check()["reason"], "fixture_start_position_changed")


class AnalyticCaveDepthReferenceTest(unittest.TestCase):
    def test_framebuffer_depth_negative_controls_and_coverage(self):
        from engine_fixture_reference import evaluate
        scene = {"schema": "engine-smoke-fixture.v1", "name": "dry-negative-cave",
                 "chunk_x_bounds": [-20, 3]}
        with tempfile.TemporaryDirectory() as temp:
            perf = Path(temp) / "perf.jsonl"
            rows = []
            for i in range(120):
                rows.append({"kind": "renderer_pixel_probe", "frame_epoch": i // 40,
                    "renderer_pixel_ray_origin_x": 22, "renderer_pixel_ray_origin_y": -36,
                    "renderer_pixel_ray_origin_z": 51, "renderer_pixel_ray_direction_x": 0,
                    "renderer_pixel_ray_direction_y": 1, "renderer_pixel_ray_direction_z": 0,
                    "renderer_pixel_opaque_hit_distance": 5.5})
            def check():
                perf.write_text("\n".join(json.dumps(r) for r in rows))
                return evaluate(scene, perf)
            self.assertEqual(check()["status"], "PASS")
            rows[0]["renderer_pixel_opaque_hit_distance"] = -1
            self.assertEqual(check()["faults"], 1, "hidden ceiling must fail")
            self.assertEqual(check()["status"], "FAIL")
            rows[0]["renderer_pixel_opaque_hit_distance"] = 8
            self.assertEqual(check()["status"], "FAIL", "a farther wall cannot mask the hole")
            for row in rows:
                row["renderer_pixel_opaque_hit_distance"] = 2
            self.assertEqual(check()["status"], "UNTESTED", "near occluders cannot prove the ceiling")
            for row in rows:
                row["renderer_pixel_opaque_hit_distance"] = 5.5
                row["frame_epoch"] = 0
            self.assertEqual(check()["status"], "UNTESTED", "one frame is insufficient")
            perf.write_text("invalid JSON")
            self.assertEqual(evaluate(scene, perf)["status"], "FAIL")




class PositiveFloorLightReferenceTest(unittest.TestCase):
    def test_nonzero_light_zero_control_material_and_coverage(self):
        from engine_floor_light_reference import evaluate
        scene = dict(name="dry-negative-cave", chunk_x_bounds=[-5,3],
            cave_bounds=dict(floor=-43.5, ceiling=-30.5,z_low=44.5,z_high=57.5),
            lighting_fixture=dict(schema="engine-positive-blocklight-fixture.v1",
                emission=15,material_id=376,emitters=[[0,-44,51]]))
        row = dict(kind="renderer_pixel_probe",renderer_pixel_ray_origin_x=0,
            renderer_pixel_ray_origin_y=-36,renderer_pixel_ray_origin_z=51,
            renderer_pixel_ray_direction_x=0,renderer_pixel_ray_direction_y=-1,
            renderer_pixel_ray_direction_z=0)
        p = "renderer_pixel_opaque_"
        row.update({p+"hit_distance":7.5,p+"surface_valid":1,p+"vertex_light_valid":1,
            p+"vertex_light_face_index":4,p+"drawable":1,p+"draw_ready":1,p+"source_index_count":6,
            p+"vertex_light_distance":0,p+"vertex_light_preview":0,p+"preview_shader_override":0,
            p+"vertex_block_light":14/15,p+"vertex_sky_light":0,p+"vertex_light_block_id":376})
        with tempfile.TemporaryDirectory() as temp:
            trace = Path(temp)/"reference.jsonl"
            def check(change=None, count=30):
                rows = []
                for i in range(count):
                    x = i % 10
                    sample = dict(row, frame_epoch=i//10)
                    sample.update({"renderer_pixel_ray_origin_x": x,
                                   p+"vertex_block_light": (14-x)/15,
                                   p+"vertex_light_block_id": 376 if x == 0 else 560})
                    sample.update(change or {})
                    rows.append(json.dumps(sample)+"\n")
                trace.write_text("".join(rows))
                return evaluate(scene,trace)
            self.assertEqual(check()["status"],"PASS")
            self.assertEqual(check({p+"vertex_block_light":0})["status"],"FAIL")
            self.assertEqual(check({p+"vertex_light_block_id":560})["status"],"FAIL")
            self.assertEqual(check({p+"vertex_light_valid":0})["status"],"UNTESTED")
            self.assertEqual(check({p+"vertex_light_preview":1})["status"],"UNTESTED")
            self.assertEqual(check(count=1)["status"],"UNTESTED")
            gpu = {p+"light_source_kind":2,p+"source_index_count":0,
                   p+"packed_light_payload_valid":1,p+"packed_light_quad_count":8}
            self.assertEqual(check(gpu)["source_counts"]["packed_gpu"],30)
            self.assertEqual(check(gpu)["status"],"PASS")
            unreadable = check(dict(gpu,**{p+"packed_light_payload_valid":0}))
            self.assertEqual(unreadable["status"],"UNTESTED")
            self.assertEqual(unreadable["skip_reasons"]["missing_readable_draw_source"],30)
            self.assertIn("frame_epochs",unreadable["missing_coverage"])
            self.assertEqual(check(dict(gpu,**{p+"vertex_block_light":0}))["status"],"FAIL")
            self.assertEqual(check({"renderer_pixel_ray_origin_x": 0,
                p+"vertex_block_light":14/15,p+"vertex_light_block_id":376})["status"],"UNTESTED")

if __name__ == "__main__":
    unittest.main()
