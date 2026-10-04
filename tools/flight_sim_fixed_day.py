#!/usr/bin/env python3
"""Run flight_sim_run.py under the established clear-day diagnostic profile.

This changes only World_164 environment values for the lifetime of the runner.
It restores the exact original world_data.json bytes in a finally block, even
when the runner exits nonzero or raises. Camera, route, and movement settings
are passed through unchanged.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def atomic_write(path: Path, payload: bytes, tag: str) -> None:
    descriptor, temp_name = tempfile.mkstemp(
        prefix=path.name + tag, dir=path.parent
    )
    temp = Path(temp_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(payload)
        os.replace(temp, path)
    finally:
        if temp.exists():
            temp.unlink()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--world", required=True)
    parser.add_argument("--time-of-day", type=float, default=0.25)
    parser.add_argument(
        "runner_args",
        nargs=argparse.REMAINDER,
        help="arguments for tools/flight_sim_run.py after --",
    )
    args = parser.parse_args()
    runner_args = list(args.runner_args)
    if runner_args and runner_args[0] == "--":
        runner_args.pop(0)
    if not runner_args:
        parser.error("pass flight_sim_run.py arguments after --")

    world_arg = None
    for index, value in enumerate(runner_args[:-1]):
        if value == "--world":
            world_arg = runner_args[index + 1]
            break
    if world_arg is not None and world_arg != args.world:
        parser.error("--world must match the wrapper's --world value")
    if world_arg is None:
        runner_args.extend(("--world", args.world))

    world_data = ROOT / "bin" / "worlds" / args.world / "world_data.json"
    if not world_data.is_file():
        parser.error(f"world metadata not found: {world_data}")
    backup = world_data.with_name(world_data.name + ".fixed-day-backup")
    if backup.exists():
        parser.error(
            f"stale backup exists at {backup}; restore it before starting another run"
        )

    original = world_data.read_bytes()
    try:
        document = json.loads(original.decode("utf-8"))
        environment = document.setdefault("environment", {})
        environment["time_frozen"] = True
        environment["time_of_day"] = args.time_of_day
        environment["weather"] = "clear"
        environment["weather_target"] = "clear"
        environment["cloud_coverage"] = 0.0
        environment["cloud_coverage_override"] = 0.0
        weather_auto = environment.setdefault("weather_auto", {})
        weather_auto["auto_enabled"] = False
        weather_auto["auto_change"] = False
        overridden = (
            json.dumps(document, ensure_ascii=False, indent=4) + "\n"
        ).encode("utf-8")
    except (UnicodeDecodeError, json.JSONDecodeError, TypeError, ValueError) as exc:
        parser.error(f"cannot prepare world metadata: {exc}")

    with backup.open("xb") as stream:
        stream.write(original)
    result_code = 1
    try:
        atomic_write(world_data, overridden, ".fixed-day.tmp")
        print(
            "fixed-day profile: "
            f"time_of_day={args.time_of_day} weather=clear clouds=0; "
            f"original_sha256={sha256(original)}",
            flush=True,
        )
        command = [
            sys.executable,
            str(ROOT / "tools" / "flight_sim_run.py"),
            *runner_args,
        ]
        result_code = subprocess.run(command, cwd=ROOT, check=False).returncode
        return result_code
    finally:
        atomic_write(world_data, original, ".restore.tmp")
        restored = world_data.read_bytes()
        if sha256(restored) != sha256(original):
            raise RuntimeError(
                f"world_data.json restoration failed; backup retained at {backup}"
            )
        backup.unlink()
        print(
            f"restored world_data.json byte-for-byte sha256={sha256(restored)}",
            flush=True,
        )


if __name__ == "__main__":
    raise SystemExit(main())
