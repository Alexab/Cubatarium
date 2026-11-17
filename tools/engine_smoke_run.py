#!/usr/bin/env python3
"""Run a native streaming scenario on an isolated copy of a saved world.

The game still runs the Release executable in bin through flight_sim_run.py.
Original saves are never launched, and the full config is restored byte-for-byte.
An exclusive lease prevents two smoke runners from sharing config.json.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import struct
import sys
import uuid
import zlib

from engine_smoke_verdict import classify, read_startup_warmup_evidence
from flight_sim_fixed_day import fixed_day_payload

ROOT = Path(__file__).resolve().parents[1]


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as source:
        for data in iter(lambda: source.read(1 << 20), b""):
            h.update(data)
    return h.hexdigest()


def world_fingerprint(world: Path) -> str:
    """Include chunk payloads and relative names, not timestamps or directory order."""
    h = hashlib.sha256()
    for path in sorted(world.rglob("*")):
        if path.is_symlink():
            raise ValueError(f"fixture symlinks are not supported: {path}")
        if path.is_file():
            h.update(path.relative_to(world).as_posix().encode("utf-8") + b"\0")
            h.update(bytes.fromhex(digest(path)))
    return h.hexdigest()


def valid_png(path: Path) -> bool:
    """Reject missing, truncated or corrupt asynchronous capture output."""
    try:
        payload = path.read_bytes()
    except OSError:
        return False
    if payload[:8] != b"\x89PNG\r\n\x1a\n":
        return False
    offset = 8
    header = False
    data = False
    while offset + 12 <= len(payload):
        length = struct.unpack_from(">I", payload, offset)[0]
        end = offset + 12 + length
        if end > len(payload):
            return False
        kind = payload[offset + 4:offset + 8]
        content = payload[offset + 8:offset + 8 + length]
        crc = struct.unpack_from(">I", payload, offset + 8 + length)[0]
        if zlib.crc32(kind + content) != crc:
            return False
        if not header:
            if kind != b"IHDR" or length != 13:
                return False
            width, height = struct.unpack_from(">II", content)
            header = width > 0 and height > 0
        data |= kind == b"IDAT"
        if kind == b"IEND":
            return header and data and length == 0 and end == len(payload)
        offset = end
    return False


def inspection_evidence(artifacts: Path, report: dict) -> dict:
    """Join stable view requests to the corresponding PNGs, never RGB guesses."""
    import math
    bot = report.get("diagnostic_bot", {})
    requested = bot.get("inspection_capture_requested_n")
    if not isinstance(requested, int) or isinstance(requested, bool) or not 0 <= requested <= 3072:
        return {"verified": 0, "reason": "missing_or_invalid_capture_count"}
    trace = artifacts / "diagnostic_bot.jsonl"
    poses: dict[int, dict] = {}
    if trace.exists():
        for line in trace.read_text(encoding="utf-8").splitlines():
            try:
                row = json.loads(line)
                if row.get("capture_request"):
                    capture_id = row["capture_id"]
                    if (not isinstance(capture_id, int) or isinstance(capture_id, bool)
                            or not 0 <= capture_id < requested):
                        return {"verified": 0, "reason": "invalid_inspection_id"}
                    if capture_id in poses:
                        return {"verified": 0, "reason": "duplicate_inspection_request"}
                    poses[capture_id] = row
            except (ValueError, TypeError, KeyError):
                return {"verified": 0, "reason": "invalid_inspection_trace"}
    verified = 0
    for capture_id in range(requested):
        pose = poses.get(capture_id)
        first = poses.get(capture_id - capture_id % 12)
        if not pose or not first:
            continue
        try:
            yaw, pitch = float(pose["yaw"]), float(pose["pitch"])
            expected_yaw = float(first["yaw"]) + 90 * ((capture_id % 12) // 3)
            expected_pitch = -45 + 45 * (capture_id % 3)
            aligned = (math.isfinite(yaw) and math.isfinite(pitch) and
                       abs(math.remainder(yaw - expected_yaw, 360)) <= 4 and
                       abs(pitch - expected_pitch) <= 2)
        except (KeyError, TypeError, ValueError):
            aligned = False
        if aligned and valid_png(artifacts / "captures" / f"inspection_{capture_id:04}.png"):
            verified += 1
    return {"verified": verified, "requested": requested,
            "reason": "inspection_views_joined_to_png"}


@contextmanager
def isolated_fixture(runtime: Path, world_name: str, artifacts: Path):
    if (not world_name or not all(c.isalnum() or c in "_.-" for c in world_name)
            or world_name in (".", "..")):
        raise ValueError("world must be a single saved-world directory name")
    source = runtime / "worlds" / world_name
    if source.is_symlink() or source.resolve().parent != (runtime / "worlds").resolve():
        raise ValueError("fixture must be a direct saved-world directory")
    if artifacts.resolve().is_relative_to((runtime / "worlds").resolve()):
        raise ValueError("artifacts must be outside the saved-world tree")
    if not (source / "world_data.json").is_file():
        raise ValueError(f"saved world metadata is missing: {source}")
    # No silent reuse: every run receives a distinct artifact directory.
    artifacts.mkdir(parents=True, exist_ok=False)
    lease = runtime / ".engine-smoke.lock"
    descriptor = os.open(lease, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    config = runtime / "config.json"
    config_bytes = None
    fixture = runtime / "worlds" / ("EngineSmoke_" + uuid.uuid4().hex)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump({"pid": os.getpid(), "artifacts": str(artifacts.resolve())}, stream)
        config_bytes = config.read_bytes()
        (artifacts / "config.original.json").write_bytes(config_bytes)
        source_hash = world_fingerprint(source)
        shutil.copytree(source, fixture)
        copied_hash = world_fingerprint(fixture)
        if copied_hash != source_hash or world_fingerprint(source) != source_hash:
            raise RuntimeError("source world changed while copying; repeat after its writer exits")
        yield fixture, source_hash
    finally:
        if config_bytes is not None:
            config.write_bytes(config_bytes)
        # Retain the exercised world for repro; never clean a computed tree here.
        lease.unlink()


def render_distance_payload(payload: bytes, radius: int) -> bytes:
    if not isinstance(radius, int) or isinstance(radius, bool) or not 1 <= radius <= 32:
        raise ValueError("render distance must be an integer from 1 to 32")
    config = json.loads(payload)
    config["render_distance_chunks"] = radius
    return (json.dumps(config, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def startup_observation_evidence(trace: Path) -> dict:
    """Verify the native driver actually stayed still and issued no movement."""
    import math
    if not trace.is_file():
        return {"status": "UNTESTED", "reason": "missing_startup_observation_trace"}
    start = None
    samples = 0
    maximum_drift = 0.0
    previous_time = -1.0
    try:
        with trace.open(encoding="utf-8") as stream:
            for line in stream:
                row = json.loads(line)
                position = row.get("position")
                timestamp = row.get("time")
                if (not isinstance(timestamp, (int, float)) or isinstance(timestamp, bool) or
                        not math.isfinite(timestamp) or timestamp <= previous_time):
                    raise ValueError("missing or nonincreasing observation time")
                previous_time = timestamp
                if (row.get("schema") != "diagnostic-bot-trace.v1" or
                        not isinstance(position, list) or len(position) != 3 or
                        any(not isinstance(v, (int, float)) or isinstance(v, bool) or
                            not math.isfinite(v) for v in position) or
                        any(row.get(k) not in (0, 1) or isinstance(row.get(k), bool)
                            for k in ("forward", "up", "down"))):
                    raise ValueError("malformed observation")
                if any(row[k] for k in ("forward", "up", "down")):
                    return {"status": "FAIL", "reason": "startup_observation_movement_input"}
                if start is None:
                    start = position
                maximum_drift = max(maximum_drift, math.dist(start, position))
                samples += 1
    except (ValueError, TypeError, AttributeError):
        return {"status": "FAIL", "reason": "malformed_startup_observation_trace"}
    if maximum_drift > 0.05:
        return {"status": "FAIL", "reason": "startup_observation_position_changed",
                "maximum_drift_blocks": maximum_drift}
    return {"status": "PASS" if samples >= 2 else "UNTESTED",
            "reason": "stationary_native_observation" if samples >= 2 else "insufficient_startup_observations",
            "samples": samples, "maximum_drift_blocks": maximum_drift}


def run(world: str, artifacts: Path, *, fly_seconds: float = 300,
        stop_seconds: float = 20, idle_seconds: float = 5,
        yaw: float = 180, pitch: float = -25,
        minimum_chunks: int = 16, fixture_scene: str | None = None,
        process_timeout: float | None = None,
        render_distance: int | None = None, sprint: bool = False,
        startup_only: bool = False) -> dict:
    if process_timeout is not None:
        import math
        if not math.isfinite(process_timeout) or process_timeout <= 0:
            raise ValueError("explicit process timeout must be finite and positive")
    runtime = ROOT / "bin"
    # On Windows opening an active executable for writing fails. This is a
    # read-only preflight via the existing runner; it never kills another app.
    import flight_sim_run
    if not flight_sim_run.exe_writable(0.5):
        raise RuntimeError("Release executable is in use; the running session was left intact")
    with isolated_fixture(runtime, world, artifacts) as (fixture, source_hash):
        if render_distance is not None:
            config_path = runtime / "config.json"
            config_path.write_bytes(render_distance_payload(config_path.read_bytes(), render_distance))
        scene = None
        if fixture_scene in ("dry-negative-cave", "wet-negative-cave", "dry-wide-negative-cave", "dry-lit-negative-cave"):
            if yaw != 180:
                raise ValueError("negative-cave route requires yaw 180")
            from engine_smoke_fixtures import prepare_negative_cave
            if fixture_scene == "dry-lit-negative-cave":
                from engine_smoke_fixtures import prepare_lit_negative_cave
                scene = prepare_lit_negative_cave(fixture, minimum_chunks)
            else:
                scene = prepare_negative_cave(fixture, minimum_chunks,
                                             flooded=fixture_scene == "wet-negative-cave",
                                             wide=fixture_scene == "dry-wide-negative-cave")
            (artifacts / "fixture.json").write_text(json.dumps(scene, indent=2) + "\n", encoding="utf-8")
        elif fixture_scene is not None:
            raise ValueError("unknown fixture scene")
        metadata = fixture / "world_data.json"
        metadata.write_bytes(fixed_day_payload(metadata.read_bytes(), 0.25))
        exe = runtime / "Cubatarium.exe"
        env = os.environ.copy()
        # Historical QA hooks can reposition cameras or modify worlds. Do not
        # inherit those switches into a correctness scenario.
        for key in list(env):
            if key.startswith(("CUBA_FLIGHT_", "CUBA_VISUAL_BLACK_", "CUBA_DEBUG_")):
                env.pop(key)
        env.update({
            "CUBATARIUM_RELIGHT_AUDIT": "1",
            "CUBA_FIXED_TIME": "0.25",
            "CUBA_VISUAL_BLACK_TRACE": "1",
            "CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS": "1",
            "CUBA_VISUAL_BLACK_TRACE_PIXEL_ON_SCREEN_RAY": "1",
            "CUBA_VISUAL_BLACK_TRACE_FOCUS_PROBES": "1",
            "CUBA_WORLD_COLUMN_SOURCE_TRACE": "1",
            "CUBA_FLIGHT_CAPTURE_DIR": str((artifacts / "captures").resolve()),
            "CUBA_FLIGHT_CAPTURE_INTERVAL_SEC": "10",
        })
        git_sha = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        env["CUBATARIUM_GIT_SHA"] = git_sha
        import math
        distance = minimum_chunks * 16 + 48
        angle = math.radians(yaw)
        rise = scene.get("route_rise", 6) if scene else 6
        bot_route = {"schema": "diagnostic-flight-route.v1", "relative": True,
                     "waypoints": [{"position": [0, rise, 0], "inspect": True},
                                   {"position": [distance * math.cos(angle), rise,
                                                 distance * math.sin(angle)], "inspect": True}]}
        if startup_only:
            bot_route["purpose"] = "startup_observation"
            bot_route["waypoints"] = []
        bot_path = artifacts / "bot-route.json"
        bot_path.write_text(json.dumps(bot_route, indent=2) + "\n", encoding="utf-8")
        command = [sys.executable, str(ROOT / "tools/flight_sim_run.py"),
                   "--world", fixture.name, "--require-existing-world",
                   "--fly-stop", "--strict-no-relocation", "--diagnostic-route", str(bot_path.resolve()),
                   "--level-forward", "--min-alt-above-sea", "0",
                   "--idle-sec", str(idle_seconds), "--fly-phase-sec", str(fly_seconds),
                   "--stop-phase-sec", str(stop_seconds), "--yaw", str(yaw),
                   "--pitch", str(pitch), "--stop-after-blocked-sec", "8",
                   "--metrics", "--artifact-root", str(artifacts.resolve()),
                   "--report", str((artifacts / "legacy-gates.json").resolve())]
        if startup_only:
            command.append("--no-hold-forward")
        if sprint:
            command.append("--sprint")
        if process_timeout is not None:
            command.extend(["--process-timeout", str(process_timeout)])
        route = {"schema": "engine-smoke-route.v1", "world_source": world,
                 "fixture_world": fixture.name, "source_world_sha256": source_hash,
                 "exe_sha256": digest(exe), "config_sha256": digest(runtime / "config.json"),
                 "git_sha": git_sha,
                 "fixture_scene": scene,
                 "prepared_world_sha256": world_fingerprint(fixture),
                 "bot_route_sha256": digest(bot_path),
                 "render_distance_override": render_distance,
                 "sprint": sprint, "startup_only": startup_only,
                 "duration_semantics": "maximum in-game budget; exits early after bot completion plus stop phase",
                 "fly_seconds": fly_seconds, "stop_seconds": stop_seconds,
                 "idle_seconds": idle_seconds, "yaw": yaw, "pitch": pitch,
                 "minimum_chunks": minimum_chunks, "command": command,
                 "diagnostic_environment": {k: v for k, v in env.items()
                                            if k.startswith(("CUBA_", "CUBATARIUM_"))}}
        (artifacts / "route.json").write_text(json.dumps(route, indent=2) + "\n", encoding="utf-8")
        with (artifacts / "runner.log").open("w", encoding="utf-8") as output:
            child = subprocess.Popen(command, cwd=ROOT, env=env, stdout=output,
                                     stderr=subprocess.STDOUT,
                                     start_new_session=sys.platform != "win32")
            try:
                child.wait()
            except BaseException:
                flight_sim_run.kill_process_tree(child.pid)
                child.wait(timeout=15)
                raise
        report_path = artifacts / "flight_sim_report.json"
        report = json.loads(report_path.read_text(encoding="utf-8")) if report_path.exists() else {}
        report["startup_warmup_evidence"] = read_startup_warmup_evidence(
            sorted((artifacts / "logs").glob("*.log.INFO.*")))
        inspection = inspection_evidence(artifacts, report)
        report["inspection_capture_files_verified_n"] = inspection["verified"]
        (artifacts / "evidence-report.json").write_text(
            json.dumps(report, indent=2) + "\n", encoding="utf-8")
        perf = Path(report["perf_jsonl"]) if report.get("perf_jsonl") else None
        verdict = classify(report, perf, minimum_chunks=minimum_chunks)
        if startup_only:
            from engine_smoke_verdict import startup_only_verdict
            verdict = startup_only_verdict(
                report, verdict, startup_observation_evidence(artifacts / "diagnostic_bot.jsonl"))
        if scene is not None and not startup_only:
            from engine_smoke_fixtures import evaluate_fixture_trace
            scene_verdict = evaluate_fixture_trace(scene, artifacts / "diagnostic_bot.jsonl")
            verdict["components"]["fixture"] = scene_verdict
            from engine_fixture_reference import evaluate as evaluate_reference
            depth_verdict = evaluate_reference(scene, perf)
            verdict["components"]["fixture_opaque_depth"] = depth_verdict
            (artifacts / "fixture-depth-reference.json").write_text(
                json.dumps(depth_verdict, indent=2) + "\n", encoding="utf-8")
            if scene_verdict["status"] == "FAIL":
                verdict["status"] = "FAIL"
            elif scene_verdict["status"] == "UNTESTED" and verdict["status"] == "PASS":
                verdict["status"] = "UNTESTED"
            if depth_verdict["status"] == "FAIL":
                verdict["status"] = "FAIL"
            elif depth_verdict["status"] == "UNTESTED" and verdict["status"] == "PASS":
                verdict["status"] = "UNTESTED"
        verdict["runner_exit_code"] = child.returncode
        verdict["inspection_evidence"] = inspection
        if not report_path.exists():
            verdict["status"] = "FAIL"
            verdict["components"]["runtime"] = {"status": "FAIL", "reason": "missing_native_report"}
        (artifacts / "smoke-verdict.json").write_text(json.dumps(verdict, indent=2) + "\n", encoding="utf-8")
        return verdict


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--world", required=True)
    parser.add_argument("--artifacts", required=True, type=Path)
    parser.add_argument("--fly-seconds", type=float, default=300,
                        help="maximum in-game route budget; completion ends the run early")
    parser.add_argument("--sprint", action="store_true", help="use native Ctrl input while moving forward")
    parser.add_argument("--startup-only", action="store_true",
                        help="observe first-world-paint readiness without route or visual acceptance")
    parser.add_argument("--stop-seconds", type=float, default=20)
    parser.add_argument("--idle-seconds", type=float, default=5)
    parser.add_argument("--yaw", type=float, default=180)
    parser.add_argument("--pitch", type=float, default=-25)
    parser.add_argument("--minimum-chunks", type=int, default=16)
    parser.add_argument("--render-distance", type=int, choices=range(1, 33),
                        help="isolated config override, restored byte-for-byte after the run")
    parser.add_argument("--process-timeout", type=float,
                        help="explicit wall deadline for bounded diagnostics; timeout is a failure")
    parser.add_argument("--fixture-scene", choices=["dry-negative-cave", "wet-negative-cave", "dry-wide-negative-cave", "dry-lit-negative-cave"])
    args = parser.parse_args()
    import math
    if (not all(math.isfinite(v) for v in [args.fly_seconds, args.stop_seconds,
                                         args.idle_seconds, args.yaw, args.pitch]) or
            min(args.fly_seconds, args.stop_seconds, args.idle_seconds) < 0 or
            args.minimum_chunks < 1 or
            (args.process_timeout is not None and
             (not math.isfinite(args.process_timeout) or args.process_timeout <= 0))):
        parser.error("finite nonnegative durations and positive coverage are required")
    verdict = run(args.world, args.artifacts.resolve(), fly_seconds=args.fly_seconds,
                  stop_seconds=args.stop_seconds, idle_seconds=args.idle_seconds,
                  yaw=args.yaw, pitch=args.pitch, minimum_chunks=args.minimum_chunks,
                  fixture_scene=args.fixture_scene, process_timeout=args.process_timeout,
                  render_distance=args.render_distance, sprint=args.sprint,
                  startup_only=args.startup_only)
    print(json.dumps(verdict, ensure_ascii=False))
    return {"PASS": 0, "FAIL": 1, "UNTESTED": 2}[verdict["status"]]


if __name__ == "__main__":
    raise SystemExit(main())
