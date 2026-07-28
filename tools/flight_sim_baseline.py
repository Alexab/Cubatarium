#!/usr/bin/env python3
"""Capture baseline metrics from existing perf reports for timeline compare."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "bin"
ANALYZE = ROOT / "tools" / "flight_sim_analyze.py"


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def analyze_perf(perf: Path, report: Path, manual_idle: bool) -> dict:
    cmd = [sys.executable, str(ANALYZE), str(perf), "--report", str(report)]
    if manual_idle:
        cmd.append("--manual-idle")
    rc = subprocess.call(cmd)
    if not report.is_file():
        return {"rc": rc, "error": "no report produced"}
    data = json.loads(report.read_text(encoding="utf-8"))
    data["analyze_rc"] = rc
    return data


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--out",
        type=Path,
        default=BIN / "iter_reports" / "baseline_snapshot.json",
    )
    ap.add_argument(
        "--manual-perf",
        type=Path,
        default=None,
        help="manual perf jsonl (default: newest in bin/logs)",
    )
    ap.add_argument(
        "--edge-report",
        type=Path,
        default=BIN / "bin" / "iter_reports" / "fps_async14_01.json",
        help="autofly edge gate report",
    )
    args = ap.parse_args()

    manual_perf = args.manual_perf
    if manual_perf is None:
        logs = BIN / "logs"
        if logs.is_dir():
            cands = sorted(logs.glob("perf_*.jsonl"), key=lambda p: p.stat().st_mtime)
            if cands:
                manual_perf = cands[-1]

    out: dict = {"created_at_utc": utc_now(), "runs": {}}
    tmp = BIN / "iter_reports" / "_baseline_tmp.json"
    tmp.parent.mkdir(parents=True, exist_ok=True)

    if manual_perf and manual_perf.is_file():
        out["runs"]["manual"] = analyze_perf(manual_perf, tmp, manual_idle=True)
        out["runs"]["manual"]["perf_jsonl"] = str(manual_perf)
        out["runs"]["manual"]["route"] = "resume save (manual-idle)"

    edge_report = args.edge_report
    if not edge_report.is_file():
        alt = BIN / "iter_reports" / "fps_async14_01.json"
        if alt.is_file():
            edge_report = alt
    if edge_report.is_file():
        edge = json.loads(edge_report.read_text(encoding="utf-8"))
        edge["route"] = "World_164 edge (-47,5) teleport-cruise"
        out["runs"]["autofly_edge"] = edge

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")
    print(f"baseline: {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
