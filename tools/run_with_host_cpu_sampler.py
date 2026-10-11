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


def _find_target_processes(
    target_name: str, command_pid: int
) -> dict[int, psutil.Process]:
    target = target_name.casefold().removesuffix(".exe")
    found: dict[int, psutil.Process] = {}
    try:
        command = psutil.Process(command_pid)
        candidates = [command, *command.children(recursive=True)]
    except (psutil.NoSuchProcess, psutil.AccessDenied):
        return found
    for process in candidates:
        try:
            name = process.name()
            if name.casefold().removesuffix(".exe") == target:
                process.cpu_percent(interval=None)  # prime this process counter
                found[process.pid] = process
        except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
            continue
    return found


def _target_snapshot(
    processes: dict[int, psutil.Process],
) -> tuple[list[dict[str, object]], float]:
    target_rows: list[dict[str, object]] = []
    for pid, process in list(processes.items()):
        try:
            cpu_pct = process.cpu_percent(interval=None)
            target_rows.append(
                {"pid": pid, "name": process.name(), "cpu_pct": round(cpu_pct, 3)}
            )
        except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
            processes.pop(pid, None)
    target_cpu_cores = sum(float(row["cpu_pct"]) for row in target_rows) / 100.0
    return target_rows, target_cpu_cores


def _top_processes_snapshot() -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for process in psutil.process_iter(["pid", "name"]):
        try:
            rows.append(
                {
                    "pid": process.info.get("pid"),
                    "name": process.info.get("name") or "",
                    "cpu_pct": round(process.cpu_percent(interval=None), 3),
                }
            )
        except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
            continue
    rows.sort(key=lambda row: float(row["cpu_pct"]), reverse=True)
    return rows[:8]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--interval-sec", type=float, default=2.0)
    parser.add_argument("--process-name", default="Cubatarium.exe")
    parser.add_argument("--process-scan-interval-sec", type=float, default=2.0)
    parser.add_argument(
        "--top-processes-interval-sec",
        type=float,
        default=0.0,
        help="optional full process listing cadence; disabled by default",
    )
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()

    command = list(args.command)
    if command[:1] == ["--"]:
        command = command[1:]
    if not command:
        parser.error("provide a command after --")
    if args.interval_sec <= 0.0:
        parser.error("--interval-sec must be positive")
    if args.process_scan_interval_sec <= 0.0:
        parser.error("--process-scan-interval-sec must be positive")
    if args.top_processes_interval_sec < 0.0:
        parser.error("--top-processes-interval-sec cannot be negative")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    psutil.cpu_percent(interval=None)  # prime the host-wide counter
    started = time.monotonic()
    child = subprocess.Popen(command)
    target_processes: dict[int, psutil.Process] = {}
    next_process_scan = started
    next_top_scan = (
        started + args.top_processes_interval_sec
        if args.top_processes_interval_sec > 0.0
        else float("inf")
    )
    with args.output.open("w", encoding="utf-8", newline="\n") as output:
        previous_sample = started
        while True:
            machine_cpu_pct = psutil.cpu_percent(interval=args.interval_sec)
            sampled_at = time.monotonic()
            if sampled_at >= next_process_scan:
                discovered = _find_target_processes(args.process_name, child.pid)
                target_processes.update(discovered)
                next_process_scan = sampled_at + args.process_scan_interval_sec
            target_rows, target_cpu_cores = _target_snapshot(target_processes)
            top_sampled = sampled_at >= next_top_scan
            top_processes = _top_processes_snapshot() if top_sampled else None
            if top_sampled:
                next_top_scan = sampled_at + args.top_processes_interval_sec
            sample = {
                "utc": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
                "elapsed_sec": round(sampled_at - started, 3),
                "sample_interval_sec": round(sampled_at - previous_sample, 3),
                "machine_cpu_pct": round(machine_cpu_pct, 2),
                "target_process_cpu_cores": round(target_cpu_cores, 3),
                "target_processes": target_rows,
                "top_processes_sampled": top_sampled,
            }
            if top_sampled:
                sample["top_processes"] = top_processes
            output.write(json.dumps(sample, separators=(",", ":")) + "\n")
            output.flush()
            previous_sample = sampled_at
            result = child.poll()
            if result is not None:
                return result


if __name__ == "__main__":
    sys.exit(main())
