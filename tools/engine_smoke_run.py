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
from analyze_mesh_publication import analyze as analyze_mesh_publication
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


def capture_receipt_matches(path: Path, pose: dict, capture_id: int,
                            previous_frame: int, previous_index: int) -> tuple[int, int]:
    """Validate encoder bytes and the pose sampled at GL_BACK readback."""
    import math
    receipt = json.loads(Path(str(path) + ".json").read_text(encoding="utf-8"))
    if (receipt.get("schema") != "diagnostic-capture-receipt.v1" or
            receipt.get("source") != "GL_BACK" or
            type(receipt.get("capture_id")) is not int or
            receipt["capture_id"] != capture_id or receipt.get("png") != path.name):
        raise ValueError("capture receipt identity mismatch")
    frame, index = receipt.get("frame_sequence"), receipt.get("capture_index")
    if (type(frame) is not int or type(index) is not int or
            frame <= previous_frame or index <= previous_index):
        raise ValueError("capture frame order mismatch")
    for value in (pose.get("position"), receipt.get("position")):
        if (not isinstance(value, list) or len(value) != 3 or
                any(type(v) not in (int, float) or not math.isfinite(v) for v in value)):
            raise ValueError("invalid readback position")
    if math.dist(pose["position"], receipt["position"]) > .05:
        raise ValueError("readback moved from requested pose")
    for key in ("yaw", "pitch"):
        a, b = pose.get(key), receipt.get(key)
        if (type(a) not in (int, float) or type(b) not in (int, float) or
                not math.isfinite(a) or not math.isfinite(b) or
                abs(math.remainder(a - b, 360)) > 2):
            raise ValueError("readback orientation mismatch")
    payload = path.read_bytes()
    width, height = struct.unpack(">II", payload[16:24])
    if (type(receipt.get("width")) is not int or type(receipt.get("height")) is not int or
            (receipt["width"], receipt["height"]) != (width, height)):
        raise ValueError("capture dimensions mismatch")
    checksum = 14695981039346656037
    for byte in payload:
        checksum = ((checksum ^ byte) * 1099511628211) & ((1 << 64) - 1)
    if receipt.get("png_fnv1a64") != f"{checksum:016x}":
        raise ValueError("capture content mismatch")
    return frame, index


def inspection_evidence(artifacts: Path, report: dict) -> dict:
    """Join stable view requests to the corresponding PNGs, never RGB guesses."""
    import math
    bot = report.get("diagnostic_bot", {})
    requested = bot.get("inspection_capture_requested_n")
    if not isinstance(requested, int) or isinstance(requested, bool) or not 0 <= requested <= 5120:
        return {"verified": 0, "reason": "missing_or_invalid_capture_count"}
    trace = artifacts / "diagnostic_bot.jsonl"
    poses: dict[int, dict] = {}
    first_observation = None
    if trace.exists():
        for line in trace.read_text(encoding="utf-8").splitlines():
            try:
                row = json.loads(line)
                if first_observation is None:
                    first_observation = row
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
    def vector(value):
        if (not isinstance(value, list) or len(value) != 3 or
                any(isinstance(v, bool) or not isinstance(v, (int, float)) or
                    not math.isfinite(v) for v in value)):
            raise ValueError("invalid inspection coordinate")
        return value

    # New routes declare their capture layout. Legacy artifacts without a
    # route retain the original twelve-pose layout for historical analysis.
    layout = None
    route_path = artifacts / "bot-route.json"
    if route_path.exists():
        try:
            route = json.loads(route_path.read_text(encoding="utf-8"))
            points = route["waypoints"]
            if (route.get("schema") != "diagnostic-flight-route.v1" or
                    not isinstance(points, list) or len(points) > 256 or
                    not isinstance(route.get("relative", False), bool)):
                raise ValueError("invalid route")
            layout = []
            for waypoint, point in enumerate(points):
                vector(point["position"])
                targets = point.get("inspection_targets", [])
                if (not isinstance(targets, list) or len(targets) > 8 or
                        not isinstance(point.get("inspect", True), bool)):
                    raise ValueError("invalid inspection layout")
                for target in targets:
                    vector(target)
                if not point.get("inspect", True):
                    continue
                first_id = len(layout)
                layout.extend((waypoint, first_id, local, None) for local in range(12))
                anchor = (vector(first_observation["position"])
                          if targets and route.get("relative", False) else [0, 0, 0])
                layout.extend((waypoint, first_id, 12 + i,
                               [a + b for a, b in zip(target, anchor)])
                              for i, target in enumerate(targets))
            if len(layout) != requested:
                return {"verified": 0, "requested": requested,
                        "reason": "inspection_route_capture_count_mismatch"}
        except (OSError, ValueError, TypeError, KeyError):
            return {"verified": 0, "reason": "invalid_inspection_route_evidence"}
    required_receipts = False
    manifest_path = artifacts / "route.json"
    if manifest_path.exists():
        try:
            required_receipts = json.loads(manifest_path.read_text(encoding="utf-8")).get(
                "inspection_receipt_schema") == "diagnostic-capture-receipt.v1"
        except (OSError, ValueError, TypeError, AttributeError):
            return {"verified": 0, "reason": "invalid_capture_manifest"}
    verified = 0
    last_frame = last_index = -1
    for capture_id in range(requested):
        pose = poses.get(capture_id)
        waypoint, first_id, local, target = (layout[capture_id] if layout is not None
            else (None, capture_id - capture_id % 12, capture_id % 12, None))
        first = poses.get(first_id)
        if not pose or not first:
            continue
        try:
            yaw, pitch = float(pose["yaw"]), float(pose["pitch"])
            expected_yaw = float(first["yaw"]) + 90 * (local // 3)
            expected_pitch = -45 + 45 * (local % 3)
            stationary = True
            if layout is not None:
                position = vector(pose["position"])
                stationary = (pose.get("state") == "inspect" and
                    pose.get("waypoints_visited") == waypoint and
                    all(pose.get(key) == 0 for key in ("forward", "up", "down")) and
                    math.dist(position, vector(first["position"])) <= 0.01)
                if target is not None:
                    delta = [a - b for a, b in zip(target, position)]
                    horizontal = math.hypot(delta[0], delta[2])
                    expected_yaw = math.degrees(math.atan2(delta[2], delta[0])) if horizontal > .001 else yaw
                    expected_pitch = max(-89, min(89, math.degrees(math.atan2(delta[1], horizontal))))
            aligned = (math.isfinite(yaw) and math.isfinite(pitch) and
                       stationary and
                       abs(math.remainder(yaw - expected_yaw, 360)) <= 4 and
                       abs(pitch - expected_pitch) <= 2)
        except (KeyError, TypeError, ValueError):
            aligned = False
        png = artifacts / "captures" / f"inspection_{capture_id:04}.png"
        if aligned and valid_png(png):
            if required_receipts:
                try:
                    last_frame, last_index = capture_receipt_matches(
                        png, pose, capture_id, last_frame, last_index)
                except (OSError, ValueError, TypeError, KeyError, AttributeError, struct.error):
                    continue
            verified += 1
    return {"verified": verified, "requested": requested,
            "reason": "inspection_views_joined_to_png",
            "join_strength": "request_readback_frame_pose_png" if required_receipts
                             else "legacy_request_filename_png"}


def capture_backpressure_evidence(artifacts: Path, inspection: dict) -> dict:
    """Require actual saturation, native bot waits, and saved joined poses."""
    retained = dropped = waits = faults = 0
    for path in sorted((artifacts / "logs").glob("*.log.INFO.*")):
        with path.open(encoding="utf-8", errors="replace") as stream:
            for line in stream:
                if "[FlightCapture]" not in line:
                    continue
                retained += "encoder_backpressure retained=" in line
                dropped += "dropped completed frame" in line
    trace = artifacts / "diagnostic_bot.jsonl"
    if trace.exists():
        with trace.open(encoding="utf-8") as stream:
            for line in stream:
                try:
                    row = json.loads(line)
                    if row.get("state") == "inspect" and row.get("capture_ready") == 0:
                        waits += 1
                        if (row.get("capture_request") or
                                any(row.get(k) != 0 for k in ("forward", "up", "down"))):
                            faults += 1
                except (ValueError, TypeError, AttributeError):
                    faults += 1
    complete = (isinstance(inspection.get("requested"), int) and
                inspection["requested"] > 0 and
                inspection.get("verified") == inspection["requested"])
    bad = dropped > 0 or faults > 0 or not complete
    return dict(status="FAIL" if bad else "PASS" if retained and waits else "UNTESTED",
                reason="encoder_backpressure_drops_or_pose_faults" if bad else
                       "bounded_encoder_backpressure_observed" if retained and waits else
                       "encoder_saturation_not_observed",
                retention_witnesses=retained, dropped_frames=dropped,
                paused_inspection_samples=waits, pose_faults=faults,
                complete_saved_inspection=complete,
                scope="native bounded encoder retention, bot nonmovement while admission blocked, "
                      "and pose-joined PNG coverage; no performance or RGB acceptance")


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


def flat_publication_payload(payload: bytes) -> bytes:
    config = json.loads(payload)
    render = config.setdefault("render", {})
    render.update(lighting_mode="flat", async_meshing=False, gpu_packed_meshing=False)
    return (json.dumps(config, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def synchronous_meshing_payload(payload: bytes) -> bytes:
    config = json.loads(payload)
    config.setdefault("render", {})["async_meshing"] = False
    return (json.dumps(config, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def unpacked_meshing_payload(payload: bytes) -> bytes:
    config = json.loads(payload)
    config.setdefault("render", {})["gpu_packed_meshing"] = False
    return (json.dumps(config, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def synchronous_meshing_evidence(path: Path) -> dict:
    """Require runtime observations; config intent alone is not mode evidence."""
    if not path.is_file():
        return {"status": "UNTESTED", "reason": "missing_meshing_mode_observations"}
    try:
        report = json.loads(path.read_text(encoding="utf-8"))
        samples = report["samples"]
        if not isinstance(samples, list):
            raise ValueError("samples must be a list")
        if not samples:
            return {"status": "UNTESTED", "reason": "empty_meshing_mode_observations"}
        values = [row["async_meshing_enabled"] for row in samples]
        if any(not isinstance(value, bool) for value in values):
            raise ValueError("runtime mode must be boolean")
    except (ValueError, KeyError, TypeError):
        return {"status": "FAIL", "reason": "malformed_meshing_mode_observations"}
    return {"status": "FAIL" if any(values) else "PASS",
            "reason": "async_meshing_observed" if any(values) else "native_async_meshing_disabled",
            "samples": len(values), "async_samples": sum(values),
            "scope": "native scheduling mode only; does not certify CPU/GPU mesher backend"}


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


def require_read_failure_probe_slice(fixture: Path, column) -> None:
    # Native DiskReadFailureProbe injects only cy=19. An absent disk frame
    # cannot exercise decode failure, even when the motor route succeeds.
    x, z = column
    chunks = fixture / "chunks"
    if not any((chunks / f"{x}_19_{z}{suffix}").is_file()
               for suffix in (".cchunk", ".json")):
        raise ValueError("disk read probe requires a persisted cy=19 target slice")


def run(world: str, artifacts: Path, *, fly_seconds: float = 300,
        stop_seconds: float = 20, idle_seconds: float = 5,
        yaw: float = 180, pitch: float = -25,
        minimum_chunks: int = 16, fixture_scene: str | None = None,
        process_timeout: float | None = None,
        render_distance: int | None = None, sprint: bool = False,
        startup_only: bool = False, capture_encode_delay_ms: int = 0,
        synchronous_meshing: bool = False, round_trip: bool = False,
        world_actions: dict | None = None, observe_bound_light: bool = False,
        source_observation_scene: dict | None = None, unpacked_meshing: bool = False,
        secondary_residency_column: tuple[int, int] | None = None,
        probe_partial_air_cancellation: bool = False,
        probe_disk_read_failure: bool = False,
        prepare_read_failure_air: bool = False,
        probe_terrain_save_failure: bool = False, walking: bool = False,
        record_replay: bool = False, debt_target: dict | None = None,
        flat_publication: bool = False, face_repair_only: bool = False,
        flow_only: bool = False) -> dict:
    if type(flow_only) is not bool or (flow_only and (not flat_publication or face_repair_only)):
        raise ValueError("Flow isolation requires the standalone Flat probe without face isolation")
    if type(face_repair_only) is not bool or (face_repair_only and not flat_publication):
        raise ValueError("Face repair isolation requires the Flat publication probe")
    if type(flat_publication) is not bool or (flat_publication and (
            not startup_only or not probe_terrain_save_failure or fixture_scene is not None)):
        raise ValueError("Flat publication requires the standalone startup terrain API probe")
    if fixture_scene == "air-pocket-negative-cave" and not startup_only:
        raise ValueError("static air-pocket fixture requires startup-only observation")
    if debt_target is not None:
        from engine_debt_target import validate as validate_debt_target
        validate_debt_target(debt_target)
        if not walking or world_actions is not None or source_observation_scene is not None:
            raise ValueError("debt target requires an exclusive authored surface walk")
    if type(record_replay) is not bool:
        raise ValueError("record_replay must be boolean")
    if type(walking) is not bool or (walking and (
            fixture_scene != "open-sky-plane" or startup_only or round_trip)):
        raise ValueError("walking requires a moving one-way open-sky-plane fixture")
    if type(probe_terrain_save_failure) is not bool or (probe_terrain_save_failure and not startup_only):
        raise ValueError("terrain save probe requires an explicit startup-only scenario")
    if type(probe_disk_read_failure) is not bool or (probe_disk_read_failure and (
            secondary_residency_column is None or probe_partial_air_cancellation)):
        raise ValueError("disk read probe requires a secondary column and excludes cancellation probe")
    if type(probe_partial_air_cancellation) is not bool or (
            probe_partial_air_cancellation and secondary_residency_column is None):
        raise ValueError("AIR cancellation probe requires an explicit secondary residency column")
    if secondary_residency_column is not None and (
            not round_trip or startup_only or
            not isinstance(secondary_residency_column, (tuple, list)) or
            len(secondary_residency_column) != 2 or
            any(type(v) is not int or not -(1 << 31) <= v < (1 << 31)
                for v in secondary_residency_column)):
        raise ValueError("secondary residency requires a round trip and two int32 coordinates")
    if type(unpacked_meshing) is not bool:
        raise ValueError("unpacked meshing request must be boolean")
    if type(observe_bound_light) is not bool:
        raise ValueError("bound light observation must be boolean")
    if not isinstance(round_trip, bool) or (round_trip and startup_only):
        raise ValueError("round trip requires a moving scenario")
    if world_actions is not None:
        from engine_world_actions import validate_plan
        validate_plan(world_actions, round_trip=round_trip)
    if source_observation_scene is not None and (
            not observe_bound_light or not round_trip or fixture_scene is not None or world_actions is None):
        raise ValueError('source observation requires retained world, bound observation and return action')
    if (isinstance(capture_encode_delay_ms, bool) or
            not isinstance(capture_encode_delay_ms, int) or
            not 0 <= capture_encode_delay_ms <= 10000):
        raise ValueError("capture encoder delay must be an integer in 0..10000 ms")
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
    from engine_build_provenance import inspect_build
    build_provenance = inspect_build(ROOT, runtime / "Cubatarium.exe")
    with isolated_fixture(runtime, world, artifacts) as (fixture, source_hash):
        if prepare_read_failure_air:
            if not probe_disk_read_failure:
                raise ValueError("upper AIR preparation requires disk read probe")
            x, z = secondary_residency_column
            metadata = json.loads((fixture / "world_data.json").read_text(encoding="utf-8"))
            if metadata.get("procedural", {}).get("max_height", 0) < 319:
                raise ValueError("upper AIR preparation requires Y=319 in world profile")
            target = fixture / "chunks" / f"{x}_19_{z}.json"
            base = target.parent / f"{x}_-4_{z}.json"
            if not (base.is_file() or base.with_suffix(".cchunk").is_file()):
                raise ValueError("AIR column preparation requires retained cy=-4 geometry")
            if target.parent.is_symlink() or target.is_symlink() or target.with_suffix(".cchunk").is_symlink():
                raise ValueError("upper AIR preparation refuses symlinks")
            if target.exists() or target.with_suffix(".cchunk").exists():
                raise ValueError("upper AIR preparation refuses existing slice")
            replacements = []
            import hashlib
            for cy in range(-4, 20):
                frame = fixture / "chunks" / f"{x}_{cy}_{z}.json"
                binary = frame.with_suffix(".cchunk")
                if frame.is_symlink() or binary.is_symlink():
                    raise ValueError("AIR column preparation refuses symlinks")
                previous = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                            for p in (frame, binary) if p.is_file()}
                binary.unlink(missing_ok=True)
                voxels = ([dict(lx=lx, ly=ly, lz=lz, type="stone")
                           for lz in range(16) for ly in range(16)
                           for lx in range(16)] if cy == -4 else [])
                frame.write_text(json.dumps(dict(format_version=4, cx=x,
                    cy=cy, cz=z, voxels=voxels)), encoding="utf-8")
                replacements.append(dict(coord=[x, cy, z], previous=previous,
                    sha256=hashlib.sha256(frame.read_bytes()).hexdigest()))
            (artifacts / "read-failure-fixture.json").write_text(json.dumps({
                "scope": "explicit isolated stone cy=-4 plus AIR cy=-3..19 fault column",
                "replacements": replacements,
                "coord": [x, 19, z], "source_unchanged": True}) + "\n", encoding="utf-8")
        if probe_disk_read_failure:
            require_read_failure_probe_slice(fixture, secondary_residency_column)
        if render_distance is not None:
            config_path = runtime / "config.json"
            config_path.write_bytes(render_distance_payload(config_path.read_bytes(), render_distance))
        if synchronous_meshing:
            config_path = runtime / "config.json"
            config_path.write_bytes(synchronous_meshing_payload(config_path.read_bytes()))
        if unpacked_meshing:
            config_path = runtime / "config.json"
            config_path.write_bytes(unpacked_meshing_payload(config_path.read_bytes()))
            (artifacts / "config.unpacked-meshing.json").write_bytes(config_path.read_bytes())
        if flat_publication:
            config_path = runtime / "config.json"
            config_path.write_bytes(flat_publication_payload(config_path.read_bytes()))
            (artifacts / "config.flat-publication.json").write_bytes(config_path.read_bytes())
        scene = None
        if fixture_scene == "air-pocket-negative-cave":
            from engine_air_pocket import prepare as prepare_pocket, static_config
            scene = prepare_pocket(fixture, minimum_chunks)
            config_path = runtime / "config.json"
            config_path.write_bytes(static_config(config_path.read_bytes()))
            (artifacts / "config.air-pocket.json").write_bytes(config_path.read_bytes())
            (artifacts / "fixture.json").write_text(json.dumps(scene, indent=2)+"\n", encoding="utf-8")
        elif fixture_scene in ("dry-negative-cave", "dry-custom-profile-cave", "wet-negative-cave", "dry-wide-negative-cave", "dry-lit-negative-cave", "dry-rgb-negative-cave", "dry-rgb-air-border", "open-sky-plane", "open-sky-cross", "open-sky-items", "open-sky-falling-creature"):
            if yaw != 180:
                raise ValueError("negative-cave route requires yaw 180")
            from engine_smoke_fixtures import prepare_negative_cave
            if fixture_scene == "open-sky-falling-creature":
                from engine_smoke_fixtures import prepare_falling_creature_plane
                scene = prepare_falling_creature_plane(fixture, minimum_chunks)
            elif fixture_scene == "open-sky-items":
                from engine_smoke_fixtures import prepare_visible_items_plane
                scene = prepare_visible_items_plane(fixture, minimum_chunks)
            elif fixture_scene == "open-sky-cross":
                from engine_smoke_fixtures import prepare_cross_plane
                scene = prepare_cross_plane(fixture, minimum_chunks)
            elif fixture_scene == "open-sky-plane":
                from engine_smoke_fixtures import prepare_open_sky_plane
                scene = prepare_open_sky_plane(fixture, minimum_chunks)
            elif fixture_scene in ("dry-lit-negative-cave", "dry-rgb-negative-cave", "dry-rgb-air-border"):
                from engine_smoke_fixtures import prepare_lit_negative_cave
                scene = prepare_lit_negative_cave(fixture, minimum_chunks, air_border=fixture_scene=="dry-rgb-air-border")
                if fixture_scene in ("dry-rgb-negative-cave", "dry-rgb-air-border"):
                    from engine_rgb_reference import prepare as prepare_rgb
                    scene = prepare_rgb(scene, fixture)
            else:
                scene = prepare_negative_cave(fixture, minimum_chunks,
                                             flooded=fixture_scene == "wet-negative-cave",
                                             wide=fixture_scene == "dry-wide-negative-cave",
                                             custom_profile=fixture_scene == "dry-custom-profile-cave")
            (artifacts / "fixture.json").write_text(json.dumps(scene, indent=2) + "\n", encoding="utf-8")
        elif fixture_scene is not None:
            raise ValueError("unknown fixture scene")
        metadata = fixture / "world_data.json"
        metadata.write_bytes(fixed_day_payload(metadata.read_bytes(), 0.25))
        exe = runtime / "Cubatarium.exe"
        env = os.environ.copy()
        env.pop("CUBATARIUM_FORCE_CPU", None)
        env.pop("CUBA_RGB_REFERENCE", None)
        # Secondary evidence is opt-in for this run, never an inherited trace.
        env.pop("CUBA_CHUNK_RESIDENCY_TRACE_SECONDARY", None)
        env.pop("CUBA_CHUNK_RESIDENCY_TRACE_SECONDARY_FILE", None)
        env.pop("CUBA_DISK_AIR_CANCEL_PROBE", None)
        env.pop("CUBA_DISK_READ_FAILURE_PROBE", None)
        env.pop("CUBA_DISK_READ_FAILURE_ARTIFACTS", None)
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
            "CUBA_FLIGHT_CAPTURE_ENCODE_DELAY_MS": str(capture_encode_delay_ms),
        })
        if fixture_scene == "air-pocket-negative-cave":
            env["CUBA_FLIGHT_AIR_POCKET_PROBE"] = "1"
        if round_trip:
            env.update({"CUBA_CHUNK_RESIDENCY_TRACE_START_COLUMN": "1",
                        "CUBA_CHUNK_RESIDENCY_TRACE_BUFFERED": "1",
                        "CUBA_CHUNK_RESIDENCY_TRACE_FILE": str((artifacts / "round-trip-residency.log").resolve())})
        if secondary_residency_column is not None:
            env.update({"CUBA_CHUNK_RESIDENCY_TRACE_SECONDARY": ",".join(map(str, secondary_residency_column)),
                        "CUBA_CHUNK_RESIDENCY_TRACE_SECONDARY_FILE": str((artifacts / "secondary-column-residency.log").resolve())})
        if probe_partial_air_cancellation:
            env["CUBA_DISK_AIR_CANCEL_PROBE"] = ",".join(map(str, secondary_residency_column))
        if probe_terrain_save_failure:
            env["CUBA_FLIGHT_QA_TERRAIN_SAVE"] = str((artifacts / "terrain-save-probe").resolve())
            if flat_publication:
                env["CUBA_FLIGHT_QA_TERRAIN_SAVE_VARIANT"] = "flat_publication"
                if face_repair_only:
                    env["CUBA_FLIGHT_QA_FACE_REPAIR_ONLY"] = "1"
                if flow_only:
                    env["CUBA_FLIGHT_QA_FLOW_ONLY"] = "1"
                env["CUBATARIUM_FORCE_CPU"] = "1"
        if probe_disk_read_failure:
            env["CUBA_DISK_READ_FAILURE_PROBE"] = ",".join(map(str, secondary_residency_column))
            env["CUBA_DISK_READ_FAILURE_ARTIFACTS"] = str((artifacts / "disk-read-save").resolve())
        if observe_bound_light or (scene and scene.get("rgb_fixture")):
            env["CUBA_RGB_REFERENCE"] = "1"
        if scene and scene.get("name") == "open-sky-plane" and not startup_only:
            # Exercise the normal FP viewmodel consumer. The default flight
            # benchmark hides it; no item/skin/save QA injection is needed.
            env["CUBA_FLIGHT_BENCH_OVERLAY"] = "0"
        git_sha = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        env["CUBATARIUM_GIT_SHA"] = git_sha
        import math
        distance = minimum_chunks * 16 + 48
        angle = math.radians(yaw)
        rise = 0 if walking else scene.get("route_rise", 6) if scene else 6
        bot_route = {"schema": "diagnostic-flight-route.v1", "relative": True,
                     "movement_mode": "walk" if walking else "fly",
                     "record_replay": record_replay,
                     "waypoints": [{"position": [0, rise, 0], "inspect": True},
                                   {"position": [distance * math.cos(angle), rise,
                                                 distance * math.sin(angle)], "inspect": True}]}
        if scene and scene.get("lighting_fixture", {}).get("emitters"):
            start = scene["starting_eye"]
            emitters = scene["lighting_fixture"]["emitters"]
            for point in bot_route["waypoints"]:
                eye = [a + b for a, b in zip(start, point["position"])]
                emitter = min(emitters, key=lambda p: math.dist(eye, p))
                # Target the top face in the same relative coordinate frame
                # as the waypoint, without changing any runtime world cell.
                point["inspection_targets"] = [[emitter[0] - start[0],
                    emitter[1] + 0.5 - start[1], emitter[2] - start[2]]]
        elif scene and scene.get("name") == "open-sky-plane":
            start = scene["starting_eye"]
            for point in bot_route["waypoints"]:
                point["inspection_targets"] = [[point["position"][0] + 2,
                    scene["plane_bounds"]["floor"] - start[1], point["position"][2]]]
        if scene and scene.get("falling_creature") and not startup_only:
            actor = scene["falling_creature"]["initial_body_origin"]
            start = scene["starting_eye"]
            # Inspect the falling actor's column with ordinary look input.
            # It lies outside the camera's own3x3 sample neighborhood.
            bot_route["waypoints"][0]["inspection_targets"] = [[
                actor[0] - start[0], scene["plane_bounds"]["floor"] - start[1],
                actor[2] - start[2]]]
        if startup_only:
            bot_route["purpose"] = "startup_observation"
            bot_route["waypoints"] = []
        elif scene and scene.get("visible_world_items"):
            start = scene["starting_eye"]
            for point, position in zip(bot_route["waypoints"], scene["visible_world_items"]["positions"]):
                target = [a-b for a, b in zip(position, start)]
                # Three ordinary inspection holds provide multiple paints of
                # each same saved body, including at low diagnostic FPS.
                point["inspection_targets"] = [target.copy() for _ in range(3)]
        if round_trip:
            import copy
            bot_route["purpose"] = "round_trip"
            bot_route["waypoints"].append(copy.deepcopy(bot_route["waypoints"][0]))
        if world_actions is not None:
            bot_route["isolated_world"] = fixture.name
            bot_route["world_actions"] = world_actions
        if source_observation_scene is not None:
            from engine_source_observation import build_route
            users=json.loads((fixture/'users.json').read_text(encoding='utf-8'))
            bot_route=build_route(bot_route,source_observation_scene,world_actions,users['Username']['position'])
            (artifacts/'source-observation-scene.json').write_text(
                json.dumps(source_observation_scene,indent=2)+'\n',encoding='utf-8')
        if debt_target is not None:
            from engine_debt_target import author as author_debt_route
            bot_route = author_debt_route(bot_route, debt_target, scene,
                fixture_hash=digest(artifacts / "fixture.json"), source_hash=source_hash,
                minimum_chunks=minimum_chunks)
            (artifacts / "debt-target.json").write_text(
                json.dumps(debt_target, indent=2) + "\n", encoding="utf-8")
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
        if startup_only or round_trip:
            # The bot supplies ordinary movement keys. The legacy native
            # hold-forward exit gate measures endpoint displacement only.
            command.append("--no-hold-forward")
        if sprint:
            command.append("--sprint")
        if process_timeout is not None:
            command.extend(["--process-timeout", str(process_timeout)])
        route = {"schema": "engine-smoke-route.v1", "world_source": world,
                 "inspection_receipt_schema": "diagnostic-capture-receipt.v1",
                 "build_provenance": build_provenance,
                 "world_seed": json.loads(metadata.read_text(encoding="utf-8")).get("world_seed"),
                 "world_profile": json.loads(metadata.read_text(encoding="utf-8")).get("procedural"),
                 "fixture_world": fixture.name, "source_world_sha256": source_hash,
                 "exe_sha256": digest(exe), "config_sha256": digest(runtime / "config.json"),
                 "git_sha": git_sha,
                 "observe_bound_light": observe_bound_light,
                 "source_observation_scene": source_observation_scene,
                 "fixture_scene": scene,
                 "prepared_world_sha256": world_fingerprint(fixture),
                 "bot_route_sha256": digest(bot_path),
                 "render_distance_override": render_distance,
                 "synchronous_meshing": synchronous_meshing,
                 "unpacked_meshing_requested": unpacked_meshing,
                 "sprint": sprint, "startup_only": startup_only, "round_trip": round_trip,
                 "walking": walking, "record_replay": record_replay,
                 "debt_target": debt_target,
                 "secondary_residency_column": secondary_residency_column,
                 "probe_partial_air_cancellation": probe_partial_air_cancellation,
                 "probe_disk_read_failure": probe_disk_read_failure,
                 "probe_terrain_save_failure": probe_terrain_save_failure,
                 "flat_publication_requested": flat_publication,
                 "face_repair_only_requested": face_repair_only,
                 "flow_only_requested": flow_only,
                 "capture_encode_delay_ms": capture_encode_delay_ms,
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
        if not startup_only:
            from engine_route_path_evidence import evaluate as evaluate_route_path
            path_evidence = evaluate_route_path(artifacts / "diagnostic_bot.jsonl", report,
                bot_route, minimum_chunks=minimum_chunks,
                scene=scene if scene and "cave_bounds" in scene else None)
            (artifacts / "ordered-route-path-evidence.json").write_text(
                json.dumps(path_evidence, indent=2) + "\n", encoding="utf-8")
            verdict["components"]["route_path"] = path_evidence
        if round_trip:
            from engine_round_trip_evidence import evaluate as evaluate_round_trip
            returned = evaluate_round_trip(artifacts, report, bot_route, minimum_chunks=minimum_chunks)
            (artifacts / "round-trip-evidence.json").write_text(json.dumps(returned, indent=2) + "\n", encoding="utf-8")
            verdict["components"]["route"] = returned
            statuses = [part["status"] for part in verdict["components"].values()]
            verdict["status"] = "FAIL" if "FAIL" in statuses else "UNTESTED" if "UNTESTED" in statuses else "PASS"
            verdict["scope"] = "motor out-and-back data lifecycle and sampled references; no general visual/light/performance acceptance"
        if scene and scene.get("custom_vertical_profile"):
            from engine_vertical_profile_evidence import evaluate as evaluate_profile
            profile_evidence = evaluate_profile(artifacts, report, scene,
                verdict["components"].get("startup", {}).get("evidence"))
            (artifacts / "vertical-profile-evidence.json").write_text(
                json.dumps(profile_evidence, indent=2) + "\n", encoding="utf-8")
            verdict["components"]["custom_vertical_profile"] = profile_evidence
        if debt_target is not None:
            from engine_debt_target import evaluate as evaluate_debt_target
            debt = evaluate_debt_target(artifacts, debt_target, report)
            (artifacts / "debt-inspection-evidence.json").write_text(
                json.dumps(debt, indent=2) + "\n", encoding="utf-8")
            verdict["components"]["debt_inspection"] = debt
        if record_replay:
            from engine_controller_replay_evidence import evaluate as evaluate_replay
            replay = evaluate_replay(artifacts,
                ROOT / "build/desktop-msvc/Release/diagnostic_flight_replay.exe")
            (artifacts / "controller-replay-evidence.json").write_text(
                json.dumps(replay, indent=2) + "\n", encoding="utf-8")
            verdict["components"]["controller_replay"] = replay
        if walking:
            from engine_surface_route_evidence import evaluate as evaluate_surface
            surface = evaluate_surface(artifacts, report, minimum_chunks=minimum_chunks)
            (artifacts / "surface-route-evidence.json").write_text(
                json.dumps(surface, indent=2) + "\n", encoding="utf-8")
            verdict["components"]["surface_route"] = surface
        if probe_disk_read_failure:
            from engine_disk_read_failure_evidence import evaluate as evaluate_read_failure
            fault = evaluate_read_failure(artifacts, report, bot_route,
                                          column=secondary_residency_column,
                                          worlds_root=runtime / "worlds")
            (artifacts / "disk-read-failure-evidence.json").write_text(
                json.dumps(fault, indent=2) + "\n", encoding="utf-8")
            verdict["components"]["disk_read_failure"] = fault
            statuses = [part["status"] for part in verdict["components"].values()]
            verdict["status"] = "FAIL" if "FAIL" in statuses else "UNTESTED" if "UNTESTED" in statuses else "PASS"
        if startup_only:
            from engine_smoke_verdict import startup_only_verdict
            verdict = startup_only_verdict(
                report, verdict, startup_observation_evidence(artifacts / "diagnostic_bot.jsonl"))
        if fixture_scene == "air-pocket-negative-cave":
            from engine_air_pocket import evaluate as evaluate_pocket
            pocket = evaluate_pocket(artifacts, report, scene)
            verdict["components"]["air_pocket_load"] = pocket
            (artifacts / "air-pocket-load-evidence.json").write_text(json.dumps(pocket, indent=2)+"\n", encoding="utf-8")
        if synchronous_meshing:
            mode = synchronous_meshing_evidence(artifacts / "movement_diagnostics.json")
            verdict["components"]["synchronous_meshing"] = mode
            if mode["status"] == "FAIL":
                verdict["status"] = "FAIL"
            elif mode["status"] == "UNTESTED" and verdict["status"] == "PASS":
                verdict["status"] = "UNTESTED"
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
        if scene is not None and scene.get("lighting_fixture") and not startup_only:
            from engine_floor_light_reference import evaluate as evaluate_floor_light
            floor_light = evaluate_floor_light(scene, perf)
            verdict["components"]["fixture_floor_draw_source_light"] = floor_light
            (artifacts / "fixture-floor-light-reference.json").write_text(
                json.dumps(floor_light, indent=2) + "\n", encoding="utf-8")
            if floor_light["status"] == "FAIL":
                verdict["status"] = "FAIL"
            elif floor_light["status"] == "UNTESTED" and verdict["status"] == "PASS":
                verdict["status"] = "UNTESTED"
        if scene is not None and (scene.get("omitted_internal_air_slices", 0) > 0 or
                                  scene.get("required_air_light_service")) and not startup_only:
            from engine_air_light_service import evaluate as evaluate_air_service
            air_service = evaluate_air_service(sorted((artifacts / "logs").glob("*.log.INFO.*")))
            verdict["components"]["fixture_air_light_service"] = air_service
            (artifacts / "fixture-air-light-service.json").write_text(
                json.dumps(air_service, indent=2) + "\n", encoding="utf-8")
            if air_service["status"] == "FAIL":
                verdict["status"] = "FAIL"
            elif air_service["status"] == "UNTESTED" and verdict["status"] == "PASS":
                verdict["status"] = "UNTESTED"
        if scene is not None and scene.get("air_border_fixture") and not startup_only:
            from engine_camera_light_reference import evaluate as evaluate_camera_light
            camera_light = evaluate_camera_light(scene, sorted((artifacts / "logs").glob("*.log.INFO.*")),
                                                 expected_frames=report.get("ingame_frames"))
            verdict["components"]["fixture_camera_field_light"] = camera_light
            (artifacts / "fixture-camera-light-reference.json").write_text(
                json.dumps(camera_light, indent=2)+"\n", encoding="utf-8")
            if camera_light["status"] == "FAIL":
                verdict["status"] = "FAIL"
            elif camera_light["status"] == "UNTESTED" and verdict["status"] == "PASS":
                verdict["status"] = "UNTESTED"
        if scene is not None and scene.get("name") == "open-sky-plane" and not startup_only:
            from engine_camera_light_reference import evaluate as evaluate_camera_light
            camera_light = evaluate_camera_light(scene, sorted((artifacts / "logs").glob("*.log.INFO.*")),
                                                 expected_frames=report.get("ingame_frames"))
            verdict["components"]["fixture_camera_field_light"] = camera_light
            (artifacts / "fixture-camera-light-reference.json").write_text(
                json.dumps(camera_light, indent=2) + "\n", encoding="utf-8")
            if camera_light["status"] == "FAIL":
                verdict["status"] = "FAIL"
            elif camera_light["status"] == "UNTESTED" and verdict["status"] == "PASS":
                verdict["status"] = "UNTESTED"
            from engine_scene_light_reference import evaluate as evaluate_scene_light
            scene_light = evaluate_scene_light(scene, sorted((artifacts / "logs").glob("*.log.INFO.*")),
                                               expected_frames=report.get("ingame_frames"))
            verdict["components"]["fixture_first_person_field_light"] = scene_light
            (artifacts / "fixture-scene-light-reference.json").write_text(
                json.dumps(scene_light, indent=2) + "\n", encoding="utf-8")
            if scene_light["status"] == "FAIL":
                verdict["status"] = "FAIL"
            elif scene_light["status"] == "UNTESTED" and verdict["status"] == "PASS":
                verdict["status"] = "UNTESTED"
            from engine_creature_light_reference import evaluate as evaluate_creature_light
            creature_light = evaluate_creature_light(
                scene, sorted((artifacts / "logs").glob("*.log.INFO.*")),
                expected_frames=report.get("ingame_frames"))
            verdict["components"]["fixture_creature_field_light"] = creature_light
            (artifacts / "fixture-creature-light-reference.json").write_text(
                json.dumps(creature_light, indent=2)+"\n", encoding="utf-8")
            if creature_light["status"] == "FAIL":
                verdict["status"] = "FAIL"
            elif creature_light["status"] == "UNTESTED" and verdict["status"] == "PASS":
                verdict["status"] = "UNTESTED"
        if scene is not None and scene.get("visible_world_items") and not startup_only:
            from engine_world_item_reference import evaluate as evaluate_world_items
            world_items = evaluate_world_items(scene, sorted((artifacts / "logs").glob("*.log.INFO.*")))
            verdict["components"]["fixture_world_item_draw_light"] = world_items
            (artifacts / "fixture-world-item-reference.json").write_text(
                json.dumps(world_items, indent=2)+"\n", encoding="utf-8")
            if world_items["status"] == "FAIL":
                verdict["status"] = "FAIL"
            elif world_items["status"] == "UNTESTED" and verdict["status"] == "PASS":
                verdict["status"] = "UNTESTED"
        if scene is not None and scene.get("falling_creature") and not startup_only:
            from engine_falling_creature_reference import evaluate as evaluate_falling_creature
            falling = evaluate_falling_creature(scene, sorted((artifacts / "logs").glob("*.log.INFO.*")))
            verdict["components"]["fixture_falling_creature_light"] = falling
            (artifacts / "fixture-falling-creature-reference.json").write_text(
                json.dumps(falling, indent=2) + "\n", encoding="utf-8")
            if falling["status"] == "FAIL":
                verdict["status"] = "FAIL"
            elif falling["status"] == "UNTESTED" and verdict["status"] == "PASS":
                verdict["status"] = "UNTESTED"
        if capture_encode_delay_ms:
            pressure = capture_backpressure_evidence(artifacts, inspection)
            verdict["components"]["capture_backpressure"] = pressure
            if pressure["status"] == "FAIL":
                verdict["status"] = "FAIL"
            elif pressure["status"] == "UNTESTED" and verdict["status"] == "PASS":
                verdict["status"] = "UNTESTED"
        if scene is not None and scene.get("rgb_fixture") and not startup_only:
            from engine_rgb_reference import evaluate as evaluate_rgb
            rgb = evaluate_rgb(scene, perf, sorted((artifacts / "logs").glob("*.log.INFO.*")))
            verdict["components"]["fixture_floor_rgb"] = rgb
            (artifacts / "fixture-floor-rgb-reference.json").write_text(
                json.dumps(rgb, indent=2)+"\n", encoding="utf-8")
            if rgb["status"] == "FAIL":
                verdict["status"] = "FAIL"
            elif rgb["status"] == "UNTESTED" and verdict["status"] == "PASS":
                verdict["status"] = "UNTESTED"
        if world_actions is not None:
            from engine_world_actions import evaluate as evaluate_world_actions
            actions = evaluate_world_actions(artifacts, fixture, world_actions,
                                            returned if round_trip else None)
            verdict["components"]["world_actions"] = actions
            (artifacts / "world-actions-evidence.json").write_text(
                json.dumps(actions, indent=2) + "\n", encoding="utf-8")
            if actions["status"] == "FAIL":
                verdict["status"] = "FAIL"
            elif actions["status"] == "UNTESTED" and verdict["status"] == "PASS":
                verdict["status"] = "UNTESTED"
        verdict["runner_exit_code"] = child.returncode
        verdict["inspection_evidence"] = inspection
        if not report_path.exists():
            verdict["status"] = "FAIL"
            verdict["components"]["runtime"] = {"status": "FAIL", "reason": "missing_native_report"}
        verdict["components"]["build_provenance"] = {
            k: v for k, v in build_provenance.items() if k != "record"}
        verdict["components"]["performance"] = {
            "status": "UNTESTED", "reason": "no_approved_performance_baseline",
            "observations": "legacy-gates.json",
            "scope": "Legacy proxy metrics retained; no matched workload/hardware baseline acceptance"}
        from engine_verdict_contract import finalize, required_for_options
        required = required_for_options(
            read_failure=probe_disk_read_failure,
            air_cancellation=probe_partial_air_cancellation,
            terrain_save_failure=probe_terrain_save_failure,
            world_actions=world_actions is not None,
            synchronous_meshing=synchronous_meshing,
            capture_backpressure=capture_encode_delay_ms > 0, walking=walking)
        if scene and scene.get("custom_vertical_profile"):
            required.append("custom_vertical_profile")
        if debt_target is not None:
            required.append("debt_inspection")
        if record_replay:
            required.append("controller_replay")
        if fixture_scene == "air-pocket-negative-cave":
            required.append("air_pocket_load")
        if not startup_only:
            required.append("route_path")
        finalize(verdict, required)
        publication = analyze_mesh_publication(artifacts, verdict)
        (artifacts / "mesh-publication-diagnostics.json").write_text(
            json.dumps(publication, indent=2) + "\n", encoding="utf-8")
        verdict["publication_diagnostics"] = {
            "artifact": "mesh-publication-diagnostics.json",
            "scope": "diagnostics only; does not alter acceptance",
        }
        (artifacts / "smoke-verdict.json").write_text(json.dumps(verdict, indent=2) + "\n", encoding="utf-8")
        return verdict


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--world", required=True)
    parser.add_argument("--artifacts", required=True, type=Path)
    parser.add_argument("--fly-seconds", type=float, default=300,
                        help="maximum in-game route budget; completion ends the run early")
    parser.add_argument("--sprint", action="store_true", help="use native Ctrl input while moving forward")
    parser.add_argument("--debt-target", type=Path,
                        help="Inspect a concrete cell selected from retained failed fixture evidence")
    parser.add_argument("--record-replay", action="store_true",
                        help="Record every controller tick/probe and verify offline decisions")
    parser.add_argument("--walking", action="store_true",
                        help="Use the normal walking motor on an open-sky-plane fixture")
    parser.add_argument("--round-trip", action="store_true",
                        help="motor return with initial-cohort eviction/fresh disk reload evidence")
    parser.add_argument("--secondary-residency-column", nargs=2, type=int, metavar=("X", "Z"),
                        help="observe a second column in a separate bounded trace; grants no acceptance")
    parser.add_argument("--probe-partial-air-cancellation", action="store_true",
                        help="one-shot real returning disk-demand cancellation/reissue for the secondary column; requires actual evidence")
    parser.add_argument("--probe-disk-read-failure", action="store_true",
                        help="corrupt five returning upper-frame read buffers then recover; controlled local edit and save rejection exercise")
    parser.add_argument("--prepare-read-failure-air", action="store_true",
                        help="explicitly prepare stone cy=-4 and AIR cy=-3..19 in isolated probe column")
    parser.add_argument("--probe-terrain-save-failure", action="store_true",
                        help="startup-only isolated UWorld save failure/retry API probe")
    parser.add_argument("--flat-publication", action="store_true",
                        help="standalone CPU/Flat publication variant of the startup API probe")
    parser.add_argument("--face-repair-only", action="store_true",
                        help="run only face repair lifecycle within the Flat publication probe")
    parser.add_argument("--flow-only", action="store_true",
                        help="run only signed-slice flow publication within the Flat probe")
    parser.add_argument("--startup-only", action="store_true",
                        help="observe first-world-paint readiness without route or visual acceptance")
    parser.add_argument("--world-actions", type=Path,
                        help="explicit bounded edit/probe plan for the isolated world copy")
    parser.add_argument("--synchronous-meshing", action="store_true",
                        help="disable async meshing in isolated config and verify native runtime mode")
    parser.add_argument("--no-packed-meshing", action="store_true",
                        help="disable GPU-resident packed output; this does not select the construction or draw backend")
    parser.add_argument("--stop-seconds", type=float, default=20)
    parser.add_argument("--idle-seconds", type=float, default=5)
    parser.add_argument("--yaw", type=float, default=180)
    parser.add_argument("--pitch", type=float, default=-25)
    parser.add_argument("--minimum-chunks", type=int, default=16)
    parser.add_argument("--capture-encode-delay-ms", type=int, default=0,
                        help="encoder-only stress delay (0..10000 ms); requires saturation evidence")
    parser.add_argument("--render-distance", type=int, choices=range(1, 33),
                        help="isolated config override, restored byte-for-byte after the run")
    parser.add_argument("--process-timeout", type=float,
                        help="explicit wall deadline for bounded diagnostics; timeout is a failure")
    parser.add_argument("--fixture-scene", choices=["air-pocket-negative-cave", "dry-negative-cave", "dry-custom-profile-cave", "wet-negative-cave", "dry-wide-negative-cave", "dry-lit-negative-cave", "dry-rgb-negative-cave", "dry-rgb-air-border", "open-sky-plane", "open-sky-cross", "open-sky-items", "open-sky-falling-creature"])
    parser.add_argument("--observe-bound-light", action="store_true",
                        help="capture actual bound GPU light and atlas witnesses without preparing a scene; does not grant a visual PASS")
    parser.add_argument('--source-observation-scene',type=Path,
                        help='declared saved RGB cave: inspect emitters before a waypoint-gated source edit; no world preparation')
    args = parser.parse_args()
    import math
    if (not all(math.isfinite(v) for v in [args.fly_seconds, args.stop_seconds,
                                         args.idle_seconds, args.yaw, args.pitch]) or
            min(args.fly_seconds, args.stop_seconds, args.idle_seconds) < 0 or
            args.minimum_chunks < 1 or
            not 0 <= args.capture_encode_delay_ms <= 10000 or
            (args.process_timeout is not None and
             (not math.isfinite(args.process_timeout) or args.process_timeout <= 0))):
        parser.error("finite nonnegative durations and positive coverage are required")
    verdict = run(args.world, args.artifacts.resolve(), fly_seconds=args.fly_seconds,
                  stop_seconds=args.stop_seconds, idle_seconds=args.idle_seconds,
                  yaw=args.yaw, pitch=args.pitch, minimum_chunks=args.minimum_chunks,
                  fixture_scene=args.fixture_scene, process_timeout=args.process_timeout,
                  render_distance=args.render_distance, sprint=args.sprint,
                  startup_only=args.startup_only,
                  synchronous_meshing=args.synchronous_meshing,
                  unpacked_meshing=args.no_packed_meshing,
                  capture_encode_delay_ms=args.capture_encode_delay_ms,
                  round_trip=args.round_trip, walking=args.walking, record_replay=args.record_replay,
                  debt_target=json.loads(args.debt_target.read_text(encoding="utf-8")) if args.debt_target else None,
                  secondary_residency_column=args.secondary_residency_column,
                  probe_partial_air_cancellation=args.probe_partial_air_cancellation,
                  probe_disk_read_failure=args.probe_disk_read_failure,
                  prepare_read_failure_air=args.prepare_read_failure_air,
                  probe_terrain_save_failure=args.probe_terrain_save_failure,
                  flat_publication=args.flat_publication,
                  face_repair_only=args.face_repair_only,
                  flow_only=args.flow_only,
                  observe_bound_light=args.observe_bound_light,
                  source_observation_scene=json.loads(args.source_observation_scene.read_text(encoding='utf-8')) if args.source_observation_scene else None,
                  world_actions=json.loads(args.world_actions.read_text(encoding="utf-8")) if args.world_actions else None)
    print(json.dumps(verdict, ensure_ascii=False))
    return {"PASS": 0, "FAIL": 1, "UNTESTED": 2}[verdict["status"]]


if __name__ == "__main__":
    raise SystemExit(main())
