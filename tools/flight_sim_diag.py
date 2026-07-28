#!/usr/bin/env python3
"""Diagnose flight-sim run outcomes (success / hang / crash / no_perf)."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "bin"
LOGS = BIN / "logs"


def newest_info_log(after_ts: float = 0.0) -> Path | None:
    if not LOGS.is_dir():
        return None
    cands = [
        p
        for p in LOGS.glob("Cubatarium.exe*.INFO.*")
        if p.is_file() and p.stat().st_mtime >= after_ts - 2.0
    ]
    if not cands:
        return None
    return max(cands, key=lambda p: p.stat().st_mtime)


def tail_lines(path: Path | None, n: int = 50) -> list[str]:
    if path is None or not path.is_file():
        return []
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return []
    if n <= 0:
        return lines
    return lines[-n:]


def classify_run_outcome(
    process_rc: int,
    hang_killed: bool,
    perf_jsonl: Path | None,
    info_tail: list[str] | None = None,
) -> str:
    if hang_killed or process_rc == 124:
        return "hang_killed"
    if perf_jsonl is None or not perf_jsonl.is_file():
        if process_rc == 0:
            return "no_perf"
        return "crash"
    if process_rc != 0:
        return "crash"
    tail_blob = "\n".join(info_tail or []).lower()
    if "fatal" in tail_blob or "segmentation fault" in tail_blob:
        return "crash"
    return "success"


def diagnose_run(
    report: Path | None,
    *,
    process_rc: int = 0,
    hang_killed: bool = False,
    perf_jsonl: Path | None = None,
    info_log: Path | None = None,
    info_tail_n: int = 50,
) -> dict[str, Any]:
    data: dict[str, Any] = {}
    if report is not None and report.is_file():
        try:
            data = json.loads(report.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, OSError):
            data = {}

    rc = int(data.get("process_rc", process_rc))
    hang = bool(data.get("hang_killed", hang_killed))
    perf_raw = data.get("perf_jsonl") or perf_jsonl
    perf_path: Path | None = None
    if isinstance(perf_raw, str) and perf_raw:
        p = Path(perf_raw)
        if p.is_file():
            perf_path = p
    elif perf_jsonl is not None and perf_jsonl.is_file():
        perf_path = perf_jsonl

    info_path = info_log
    if info_path is None and isinstance(data.get("info_log"), str):
        p = Path(data["info_log"])
        if p.is_file():
            info_path = p

    info_tail = tail_lines(info_path, info_tail_n)
    outcome = classify_run_outcome(rc, hang, perf_path, info_tail)

    return {
        "run_outcome": outcome,
        "process_rc": rc,
        "hang_killed": hang,
        "info_log": str(info_path) if info_path else "",
        "info_tail": info_tail,
        "perf_jsonl": str(perf_path) if perf_path else "",
    }


def annotate_report_run(
    report: Path,
    process_rc: int,
    hang_killed: bool,
    perf_jsonl: Path | None = None,
    info_log: Path | None = None,
) -> None:
    if not report.is_file():
        return
    try:
        data = json.loads(report.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return

    diag = diagnose_run(
        report,
        process_rc=process_rc,
        hang_killed=hang_killed,
        perf_jsonl=perf_jsonl,
        info_log=info_log,
    )
    data["hang_killed"] = diag["hang_killed"]
    data["process_rc"] = diag["process_rc"]
    data["run_outcome"] = diag["run_outcome"]
    data["info_log"] = diag["info_log"]
    data["info_tail"] = diag["info_tail"]
    if diag["perf_jsonl"]:
        data["perf_jsonl"] = diag["perf_jsonl"]
    if diag["run_outcome"] != "success":
        data["pass"] = False
    report.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
