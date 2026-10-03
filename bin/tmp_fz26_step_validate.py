#!/usr/bin/env python3
"""FZ2.6 step validation: build, unit, no-teleport autofly, per-track gates."""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "bin"
LOGS = BIN / "logs"
BUILD = ROOT / "build" / "desktop-msvc" / "Release"
RESULTS = BIN / "fz26_step_results.jsonl"

BASELINE_FZ26 = LOGS / "perf_20260823-125933_8084.jsonl"

STEP_GATES: dict[str, list[str]] = {
    "FZ26-Perf0": ["apply_binding_time_pct"],
    "FZ26-Perf1": [
        "relight_apply_n_steady",
        "apply_util_steady",
        "unit_apply_light_ms",
        "unit_apply_install_ms",
        "sim_steady_med",
    ],
    "FZ26-Perf2": ["opaque_refs_steady", "VB_steady_med"],
    "FZ26-Perf3": [
        "stream_steady_med",
        "phase_budget_over_pct",
        "sim_steady_med",
        "enter_wall_p90",
    ],
    "FZ26-P0a": ["vb_blink_steady", "VB_steady_med"],
    "FZ26-P0b": ["stalled_tail_max", "mark_relit_steady"],
    "FZ26-P1": ["VB_steady_med", "stalled_tail_max"],
    "FZ26-C8": ["ALL"],
}

STEP_SCENARIO: dict[str, str] = {
    "FZ26-Perf0": "fz-manual-plateau",
    "FZ26-Perf1": "fz-manual-long",
    "FZ26-Perf2": "fz-manual-long",
    "FZ26-Perf3": "fz-manual-parity",
    "FZ26-P0a": "fz-manual-long",
    "FZ26-P0b": "fz-manual-long",
    "FZ26-P1": "fz-manual-long",
    "FZ26-C8": "fz-manual-long",
}

REMEDIATION: dict[str, str] = {
    "relight_apply_n_steady": "Perf-1: slim atomic unit, idle slice 16ms, no pipeline split",
    "apply_util_steady": "Perf-1: binding-aware slice + light-only cap math",
    "unit_apply_light_ms": "Perf-1: slim light merge path on consume",
    "unit_apply_install_ms": "Perf-1: primary_only MarkRelit, no neighbor fanout",
    "sim_steady_med": "Perf-3: stream prep diet + movement EMA decouple",
    "opaque_refs_steady": "Perf-2: consumer-bound backpressure, revert producer floors",
    "VB_steady_med": "P0-A/P1: VB SoT + completion FSM, not producer floors",
    "stream_steady_med": "Perf-3: cut prep_* hot path in UpdateStreaming",
    "phase_budget_over_pct": "Perf-3: honest phase budget or prep reduction",
    "enter_wall_p90": "Perf-2/3: reduce mesh_schedule tax + stream hitch",
    "vb_blink_steady": "P0-A: VB SoT hysteresis, remove telem early-out",
    "stalled_tail_max": "P0-B: mesh_drain not mesh_schedule under consume",
    "mark_relit_steady": "P0-B: stalled ticket mesh_drain coupling",
    "apply_binding_time_pct": "Perf-0: apply_binding telem + BRM forensics",
}

MUST_NOT_REGRESS = {
    "black_sticky": ("sum", 0, "le"),
    "enter_wall_p90": ("p90", 280, "lt"),
}


def run(cmd: list[str], *, cwd: Path | None = None) -> int:
    print("+", " ".join(str(c) for c in cmd), flush=True)
    return subprocess.call(cmd, cwd=cwd or ROOT)


def newest_perf(since: float) -> Path | None:
    cands = [p for p in LOGS.glob("perf_*.jsonl") if p.stat().st_mtime >= since - 2.0]
    if not cands:
        cands = sorted(LOGS.glob("perf_*.jsonl"), key=lambda p: p.stat().st_mtime)
    return cands[-1] if cands else None


def parse_gates(text: str) -> dict[str, str]:
    out = {}
    for m in re.finditer(
        r"^\s+(\w+):\s+([\d.]+|SHORT_FLIGHT skip steady|\(skip[^\)]*\))\s+\(target",
        text,
        re.M,
    ):
        val = m.group(2)
        if val.startswith("SHORT") or val.startswith("(skip"):
            out[m.group(1)] = "SKIP"
        else:
            status_m = re.search(
                rf"^\s+{re.escape(m.group(1))}:.*\s+(PASS|FAIL)\s*$",
                text,
                re.M,
            )
            out[m.group(1)] = status_m.group(1) if status_m else "UNKNOWN"
    for m in re.finditer(
        r"^\s+(\w+):\s+([\d.]+)\s+\(target[^)]+\)\s+(PASS|FAIL)",
        text,
        re.M,
    ):
        out[m.group(1)] = m.group(3)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--step", required=True)
    ap.add_argument("--scenario", default=None)
    ap.add_argument("--baseline-manual", default=str(BASELINE_FZ26))
    ap.add_argument("--build", action="store_true")
    ap.add_argument("--skip-autofly", action="store_true")
    ap.add_argument("--skip-build", action="store_true")
    ap.add_argument("--perf", default=None, help="perf jsonl when --skip-autofly")
    args = ap.parse_args()

    step = args.step if args.step.startswith("FZ26") else f"FZ26-{args.step}"
    scenario = args.scenario or STEP_SCENARIO.get(step, "fz-manual-long")
    gate_names = STEP_GATES.get(step, STEP_GATES["FZ26-C8"])

    if args.build and not args.skip_build:
        rc = run(
            [
                "cmake",
                "--build",
                "build/desktop-msvc",
                "--config",
                "Release",
                "--target",
                "Cubatarium",
                "miss_first_mesh_class_test",
                "frame_streaming_budget_test",
            ]
        )
        if rc != 0:
            return rc
        for exe in ("miss_first_mesh_class_test.exe", "frame_streaming_budget_test.exe"):
            p = BUILD / exe
            if p.is_file():
                rc = run([str(p)])
                if rc != 0:
                    return rc

    perf_path: Path | None = None
    if not args.skip_autofly:
        t0 = time.time()
        stamp = time.strftime("%Y%m%d-%H%M%S")
        report = BIN / "suite_reports" / f"{stamp}_{scenario}.json"
        report.parent.mkdir(parents=True, exist_ok=True)
        rc = run(
            [
                sys.executable,
                str(ROOT / "tools" / "flight_sim_run.py"),
                "--scenario",
                scenario,
                "--phase-id",
                step,
                "--report",
                str(report),
            ]
        )
        if rc not in (0, 1, 2):
            return 3
        perf_path = newest_perf(t0)
        if perf_path is None:
            print("FAIL: no perf jsonl after autofly", file=sys.stderr)
            return 3
        print(f"perf: {perf_path}", flush=True)
    else:
        if args.perf:
            perf_path = Path(args.perf)
        else:
            perf_path = newest_perf(time.time())
        if perf_path is None or not perf_path.is_file():
            print("FAIL: no perf jsonl", file=sys.stderr)
            return 3

    gate_proc = subprocess.run(
        [sys.executable, str(BIN / "tmp_fz2_gate_check.py"), str(perf_path)],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    print(gate_proc.stdout, end="")

    for script in ("tmp_fz26_budget_reality.py", "tmp_fz25_bottleneck.py"):
        p = BIN / script
        if p.is_file():
            subprocess.run([sys.executable, str(p), str(perf_path)], cwd=ROOT)

    baseline = Path(args.baseline_manual)
    if baseline.is_file():
        subprocess.run(
            [sys.executable, str(BIN / "tmp_fz26_delta.py"), str(baseline), str(perf_path)],
            cwd=ROOT,
        )

    gates = parse_gates(gate_proc.stdout)
    print(f"=== FZ26 step gates: {step} ===")
    track_fail = False
    if "ALL" in gate_names:
        check = [k for k, v in gates.items() if v == "FAIL"]
    else:
        check = []
        for g in gate_names:
            st = gates.get(g, "MISSING")
            print(f"  {g}: {st}")
            if st == "FAIL" or st == "MISSING":
                track_fail = True
                hint = REMEDIATION.get(g, "see plan track owner")
                print(f"    REMEDIATION: {hint}")
        check = [g for g in gate_names if gates.get(g) == "FAIL"]

    if "ALL" in gate_names:
        for g in check:
            print(f"  FAIL: {g}")
            track_fail = True
    elif gate_names == ["apply_binding_time_pct"]:
        st = gates.get("apply_binding_time_pct", "MISSING")
        if st in ("MISSING", "SKIP", "UNKNOWN"):
            print(f"  apply_binding_time_pct: {st} (need post-FZ26 perf log)")
            track_fail = st not in ("SKIP",)
        elif st == "FAIL":
            track_fail = True

    audit_rc = subprocess.run(
        [
            sys.executable,
            str(BIN / "tmp_fz26_metric_audit.py"),
            str(perf_path),
            str(baseline) if baseline.is_file() else "",
        ],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    print(audit_rc.stdout, end="")
    if audit_rc.stderr:
        print(audit_rc.stderr, file=sys.stderr, end="")

    record = {
        "ts": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "step": step,
        "scenario": scenario,
        "perf": str(perf_path),
        "gates": gates,
        "track_fail": track_fail,
    }
    RESULTS.parent.mkdir(parents=True, exist_ok=True)
    with RESULTS.open("a", encoding="utf-8") as f:
        f.write(json.dumps(record) + "\n")

    if track_fail:
        return 1
    if audit_rc.returncode != 0 and step == "FZ26-C8":
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
