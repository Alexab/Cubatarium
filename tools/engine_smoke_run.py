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
import sys
import uuid

from engine_smoke_verdict import classify
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


def run(world: str, artifacts: Path, *, fly_seconds: float = 75,
        stop_seconds: float = 20, idle_seconds: float = 5,
        yaw: float = 180, pitch: float = -25,
        minimum_chunks: int = 16) -> dict:
    runtime = ROOT / "bin"
    # On Windows opening an active executable for writing fails. This is a
    # read-only preflight via the existing runner; it never kills another app.
    import flight_sim_run
    if not flight_sim_run.exe_writable(0.5):
        raise RuntimeError("Release executable is in use; the running session was left intact")
    with isolated_fixture(runtime, world, artifacts) as (fixture, source_hash):
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
        command = [sys.executable, str(ROOT / "tools/flight_sim_run.py"),
                   "--world", fixture.name, "--require-existing-world",
                   "--fly-stop", "--strict-no-relocation", "--level-forward", "--min-alt-above-sea", "0",
                   "--idle-sec", str(idle_seconds), "--fly-phase-sec", str(fly_seconds),
                   "--stop-phase-sec", str(stop_seconds), "--yaw", str(yaw),
                   "--pitch", str(pitch), "--stop-after-blocked-sec", "8",
                   "--metrics", "--artifact-root", str(artifacts.resolve()),
                   "--report", str((artifacts / "legacy-gates.json").resolve())]
        route = {"schema": "engine-smoke-route.v1", "world_source": world,
                 "fixture_world": fixture.name, "source_world_sha256": source_hash,
                 "exe_sha256": digest(exe), "config_sha256": digest(runtime / "config.json"),
                 "git_sha": git_sha,
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
        perf = Path(report["perf_jsonl"]) if report.get("perf_jsonl") else None
        verdict = classify(report, perf, minimum_chunks=minimum_chunks)
        verdict["runner_exit_code"] = child.returncode
        if not report_path.exists():
            verdict["status"] = "FAIL"
            verdict["components"]["runtime"] = {"status": "FAIL", "reason": "missing_native_report"}
        (artifacts / "smoke-verdict.json").write_text(json.dumps(verdict, indent=2) + "\n", encoding="utf-8")
        return verdict


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--world", required=True)
    parser.add_argument("--artifacts", required=True, type=Path)
    parser.add_argument("--fly-seconds", type=float, default=75)
    parser.add_argument("--stop-seconds", type=float, default=20)
    parser.add_argument("--idle-seconds", type=float, default=5)
    parser.add_argument("--yaw", type=float, default=180)
    parser.add_argument("--pitch", type=float, default=-25)
    parser.add_argument("--minimum-chunks", type=int, default=16)
    args = parser.parse_args()
    import math
    if (not all(math.isfinite(v) for v in [args.fly_seconds, args.stop_seconds,
                                         args.idle_seconds, args.yaw, args.pitch]) or
            min(args.fly_seconds, args.stop_seconds, args.idle_seconds) < 0 or
            args.minimum_chunks < 1):
        parser.error("finite nonnegative durations and positive coverage are required")
    verdict = run(args.world, args.artifacts.resolve(), fly_seconds=args.fly_seconds,
                  stop_seconds=args.stop_seconds, idle_seconds=args.idle_seconds,
                  yaw=args.yaw, pitch=args.pitch, minimum_chunks=args.minimum_chunks)
    print(json.dumps(verdict, ensure_ascii=False))
    return {"PASS": 0, "FAIL": 1, "UNTESTED": 2}[verdict["status"]]


if __name__ == "__main__":
    raise SystemExit(main())
