#!/usr/bin/env python3
"""Run a command while recording host and target-process CPU usage to JSONL."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

try:
    import psutil
except ImportError as exc:  # pragma: no cover - depends on local runtime
    raise SystemExit("psutil is required for host CPU sampling") from exc


def _process_snapshot(target_name: str) -> dict[str, object]:
    target = target_name.casefold().removesuffix(".exe")
    rows: list[dict[str, object]] = []
    target_rows: list[dict[str, object]] = []
    for process in psutil.process_iter(["pid", "name"]):
        try:
            name = process.info.get("name") or ""
            cpu_pct = process.cpu_percent(interval=None)
            row = {
                "pid": process.info.get("pid"),
                "name": name,
                "cpu_pct": round(cpu_pct, 3),
            }
            rows.append(row)
            if name.casefold().removesuffix(".exe") == target:
                target_rows.append(row)
        except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
            continue

    target_cpu_cores = sum(float(row["cpu_pct"]) for row in target_rows) / 100.0
    rows.sort(key=lambda row: float(row["cpu_pct"]), reverse=True)
    return {
        "target_process_cpu_cores": round(target_cpu_cores, 3),
        "target_processes": target_rows,
        "process_count": len(rows),
        "top_processes": rows[:8],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--interval-sec", type=float, default=2.0)
    parser.add_argument("--process-name", default="Cubatarium.exe")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()

    command = list(args.command)
    if command[:1] == ["--"]:
        command = command[1:]
    if not command:
        parser.error("provide a command after --")
    if args.interval_sec <= 0.0:
        parser.error("--interval-sec must be positive")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    psutil.cpu_percent(interval=None)  # prime the host-wide counter
    started = time.monotonic()
    child = subprocess.Popen(command)
    with args.output.open("w", encoding="utf-8", newline="\n") as output:
        while True:
            time.sleep(args.interval_sec)
            sample = {
                "utc": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
                "elapsed_sec": round(time.monotonic() - started, 3),
                "machine_cpu_pct": round(psutil.cpu_percent(interval=None), 2),
                **_process_snapshot(args.process_name),
            }
            output.write(json.dumps(sample, separators=(",", ":")) + "\n")
            output.flush()
            result = child.poll()
            if result is not None:
                return result


if __name__ == "__main__":
    sys.exit(main())
