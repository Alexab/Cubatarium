#!/usr/bin/env python3
"""Run iterative flight-sim loops with diagnostics and log analysis.

This tool automates:
1) reproducible flythrough runs;
2) collection of perf_*.jsonl + INFO logs;
3) per-iteration root-cause classification;
4) stop criteria checks for FPS / sticky-dark recovery trends.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "bin"
LOGS = BIN / "logs"
RUNNER = ROOT / "tools" / "flight_sim_run.py"
DIAG = ROOT / "tools" / "flight_sim_diag.py"


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def newest_info_log(after_ts: float) -> Path | None:
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


def run_cmd(cmd: list[str], cwd: Path) -> int:
    print(">", " ".join(cmd), flush=True)
    return subprocess.call(cmd, cwd=str(cwd))


def tail_lines(path: Path, n: int = 40) -> list[str]:
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return []
    if n <= 0:
        return lines
    return lines[-n:]


def load_json(path: Path) -> dict[str, Any] | None:
    if not path.is_file():
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return None


def read_perf_from_phase_history(phase_id: str) -> Path | None:
    hist = BIN / "flight_sim_phase_history.jsonl"
    if not hist.is_file():
        return None
    try:
        rows = [
            json.loads(line)
            for line in hist.read_text(encoding="utf-8").splitlines()
            if line.strip()
        ]
    except (json.JSONDecodeError, OSError):
        return None
    for row in reversed(rows):
        if str(row.get("phase") or "") != phase_id:
            continue
        perf = row.get("perf")
        if isinstance(perf, str) and perf:
            p = Path(perf)
            if p.is_file():
                return p
    return None


def classify(metrics: dict[str, Any], info_tail: list[str]) -> list[str]:
    reasons: list[str] = []
    wall = float(metrics.get("wall_ms_med") or 0.0)
    spike_max = float(metrics.get("spike_max_wall") or 0.0)
    prep_max = float(metrics.get("mesh_emerge_prep_ms_max") or 0.0)
    move_max = float(metrics.get("do_movement_ms_max") or 0.0)
    pending_med = float(metrics.get("pending_light_focus_med") or 0.0)
    dark_med = float(metrics.get("dark_face_near_n_med") or 0.0)
    focus_dark_max = float(metrics.get("focus_dark_mesh_max") or 0.0)
    relight_done = float(metrics.get("relight_completed_n_med") or 0.0)
    dirty_med = float(metrics.get("dirty_med") or 0.0)
    mesh_async_med = float(metrics.get("mesh_async_med") or 0.0)

    if prep_max >= 250.0 or move_max >= 250.0:
        reasons.append("main_thread_hitch_mesh_or_movement")
    if pending_med >= 10.0 and relight_done <= 0.5:
        reasons.append("light_debt_plateau")
    if dirty_med >= 250.0 and mesh_async_med <= 2.0:
        reasons.append("dirty_backlog_without_drain")
    if dark_med >= 500.0 or focus_dark_max > 0.0:
        reasons.append("sticky_dark_faces")
    if wall >= 45.0 or spike_max >= 200.0:
        reasons.append("fps_regression")

    tail_blob = "\n".join(info_tail).lower()
    if "hang" in tail_blob or "timeout" in tail_blob:
        reasons.append("runtime_hang_or_timeout")
    if "error" in tail_blob or "fatal" in tail_blob:
        if "runtime_hang_or_timeout" not in reasons:
            reasons.append("runtime_crash")
        else:
            reasons.append("runtime_error")

    if not reasons:
        reasons.append("no_major_regression_detected")
    return reasons


def suggest_actions(reasons: list[str]) -> list[str]:
    actions: list[str] = []
    if "main_thread_hitch_mesh_or_movement" in reasons:
        actions.append(
            "Cap synchronous work in MeshEmerge; keep relight/remesh admission async-only."
        )
    if "light_debt_plateau" in reasons:
        actions.append(
            "Increase near-focus relight promotion/drain floor and verify async results drain each frame."
        )
    if "dirty_backlog_without_drain" in reasons:
        actions.append(
            "Reduce new dirty fanout while async backlog is high; prioritize completion over enqueue."
        )
    if "sticky_dark_faces" in reasons:
        actions.append(
            "Prioritize stale-dark column relight/remesh and confirm PendingLight gate clears on completion."
        )
    if "fps_regression" in reasons:
        actions.append(
            "Re-check spike classes from report and tighten budgets for the dominant path."
        )
    if not actions:
        actions.append("No follow-up action required for this iteration.")
    return actions


@dataclass
class StopCriteria:
    max_spike_wall: float
    max_wall_med: float
    max_pending_focus_med: float
    max_focus_dark_mesh: float


def stop_ok(metrics: dict[str, Any], c: StopCriteria) -> bool:
    spike_max = float(metrics.get("spike_max_wall") or 0.0)
    wall_med = float(metrics.get("wall_ms_med") or 0.0)
    pending_med = float(metrics.get("pending_light_focus_med") or 0.0)
    focus_dark_max = float(metrics.get("focus_dark_mesh_max") or 0.0)
    return (
        spike_max <= c.max_spike_wall
        and wall_med <= c.max_wall_med
        and pending_med <= c.max_pending_focus_med
        and focus_dark_max <= c.max_focus_dark_mesh
    )


def build_timeline_summary(runs: list[dict[str, Any]], out: Path) -> None:
    rows = []
    for r in runs:
        rows.append(
            {
                "phase": r.get("phase"),
                "run_outcome": r.get("run_outcome", ""),
                "rc": r.get("rc"),
                "pass": r.get("pass"),
                "holes_rate": (r.get("metrics") or {}).get("holes_rate"),
                "wall_ms_med": (r.get("metrics") or {}).get("wall_ms_med"),
                "pending_light_focus_med": (r.get("metrics") or {}).get(
                    "pending_light_focus_med"
                ),
                "spike_max_wall": (r.get("metrics") or {}).get("spike_max_wall"),
            }
        )
    payload = {"updated_at_utc": utc_now(), "phases": rows}
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def resolve_report_path(report: Path) -> Path:
    if report.is_file():
        return report
    alt = BIN / "bin" / report.relative_to(BIN) if report.is_relative_to(BIN) else report
    if alt.is_file():
        return alt
    return report


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--world", default="World_164")
    ap.add_argument("--iterations", type=int, default=3)
    ap.add_argument("--phase-prefix", default="iter")
    ap.add_argument("--report-dir", type=Path, default=BIN / "iter_reports")
    ap.add_argument("--summary", type=Path, default=BIN / "flight_sim_iterate_summary.json")
    ap.add_argument("--build-first", action="store_true")
    ap.add_argument("--max-spike-wall", type=float, default=200.0)
    ap.add_argument("--max-wall-med", type=float, default=45.0)
    ap.add_argument("--max-pending-focus-med", type=float, default=8.0)
    ap.add_argument("--max-focus-dark-mesh", type=float, default=0.0)
    ap.add_argument("--process-timeout", type=float, default=420.0)
    ap.add_argument(
        "--backend",
        choices=["gpu", "cpu"],
        default="gpu",
        help="force CPU mesher via CUBATARIUM_FORCE_CPU=1 when cpu",
    )
    ap.add_argument(
        "--timeline-summary",
        type=Path,
        default=BIN / "iter_reports" / "timeline_summary.json",
    )
    args = ap.parse_args()

    if not RUNNER.is_file():
        print(f"FAIL: missing {RUNNER}", file=sys.stderr)
        return 2

    args.report_dir.mkdir(parents=True, exist_ok=True)
    stop_cfg = StopCriteria(
        max_spike_wall=args.max_spike_wall,
        max_wall_med=args.max_wall_med,
        max_pending_focus_med=args.max_pending_focus_med,
        max_focus_dark_mesh=args.max_focus_dark_mesh,
    )

    out: dict[str, Any] = {
        "created_at_utc": utc_now(),
        "world": args.world,
        "iterations_requested": args.iterations,
        "stop_criteria": {
            "max_spike_wall": stop_cfg.max_spike_wall,
            "max_wall_med": stop_cfg.max_wall_med,
            "max_pending_focus_med": stop_cfg.max_pending_focus_med,
            "max_focus_dark_mesh": stop_cfg.max_focus_dark_mesh,
        },
        "runs": [],
        "stopped_early": False,
    }

    for i in range(1, max(1, args.iterations) + 1):
        phase = f"{args.phase_prefix}_{i:02d}"
        report = args.report_dir / f"{phase}.json"
        t0 = report.stat().st_mtime if report.exists() else 0.0
        started = utc_now()

        cmd = [
            sys.executable,
            str(RUNNER),
            "--world",
            args.world,
            "--teleport-cruise",
            "--seconds",
            "130",
            "--fly-stop",
            "--fly-phase-sec",
            "45",
            "--stop-phase-sec",
            "60",
            "--idle-sec",
            "8",
            "--process-timeout",
            str(args.process_timeout),
            "--phase-id",
            phase,
            "--report",
            str(report),
        ]
        if i == 1 and args.build_first:
            cmd.append("--build")
        if args.backend == "cpu":
            import os

            env = os.environ.copy()
            env["CUBATARIUM_FORCE_CPU"] = "1"
            print(">", " ".join(cmd), flush=True)
            rc = subprocess.call(cmd, cwd=str(BIN), env=env)
        else:
            rc = run_cmd(cmd, BIN)
        report_resolved = resolve_report_path(report)
        data = load_json(report_resolved) or {}
        metrics = (data.get("metrics") or {}) if isinstance(data, dict) else {}
        perf_path = read_perf_from_phase_history(phase)
        info_path = newest_info_log(t0)
        info_tail = tail_lines(info_path, n=50) if info_path else []
        run_outcome = data.get("run_outcome", "")
        if not run_outcome and DIAG.is_file():
            import importlib.util

            spec = importlib.util.spec_from_file_location("flight_sim_diag", DIAG)
            if spec and spec.loader:
                mod = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(mod)
                hang = bool(data.get("hang_killed"))
                perf_raw = data.get("perf_jsonl") or ""
                perf_p = Path(perf_raw) if perf_raw else None
                run_outcome = mod.classify_run_outcome(
                    rc, hang, perf_p if perf_p and perf_p.is_file() else None, info_tail
                )
        reasons = classify(metrics, info_tail)
        if run_outcome == "no_perf":
            reasons.append("no_perf")
        elif run_outcome == "crash":
            if "runtime_crash" not in reasons:
                reasons.append("runtime_crash")
        actions = suggest_actions(reasons)
        passed = bool(data.get("pass")) if isinstance(data, dict) else False
        criteria_ok = stop_ok(metrics, stop_cfg)

        run_row = {
            "phase": phase,
            "started_at_utc": started,
            "rc": rc,
            "run_outcome": run_outcome,
            "report_path": str(report_resolved),
            "perf_jsonl": str(perf_path) if perf_path else "",
            "info_log": str(info_path) if info_path else "",
            "pass": passed,
            "criteria_ok": criteria_ok,
            "metrics": {
                "wall_ms_med": metrics.get("wall_ms_med"),
                "spike_max_wall": metrics.get("spike_max_wall"),
                "spike_count": metrics.get("spike_count"),
                "mesh_emerge_prep_ms_max": metrics.get("mesh_emerge_prep_ms_max"),
                "do_movement_ms_max": metrics.get("do_movement_ms_max"),
                "pending_light_focus_med": metrics.get("pending_light_focus_med"),
                "dark_face_near_n_med": metrics.get("dark_face_near_n_med"),
                "focus_dark_mesh_max": metrics.get("focus_dark_mesh_max"),
                "relight_completed_n_med": metrics.get("relight_completed_n_med"),
                "dirty_med": metrics.get("dirty_med"),
                "mesh_async_med": metrics.get("mesh_async_med"),
            },
            "diagnosis": reasons,
            "recommended_actions": actions,
        }
        out["runs"].append(run_row)
        print(json.dumps(run_row, indent=2, ensure_ascii=False), flush=True)

        if rc != 0:
            out["stopped_early"] = True
            out["stop_reason"] = f"run_failed_rc_{rc}"
            break
        if criteria_ok:
            out["stopped_early"] = True
            out["stop_reason"] = "criteria_satisfied"
            break

    out["finished_at_utc"] = utc_now()
    args.summary.parent.mkdir(parents=True, exist_ok=True)
    args.summary.write_text(
        json.dumps(out, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    build_timeline_summary(out["runs"], args.timeline_summary)
    print(f"summary: {args.summary}")
    print(f"timeline: {args.timeline_summary}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
