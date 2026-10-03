#!/usr/bin/env python3
"""FZ2.7 step validation: build bin/Release, unit, no-teleport autofly, gates."""
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
BUILD = BIN / "Release"
RESULTS = BIN / "fz27_step_results.jsonl"

BASELINE_FZ27 = LOGS / "perf_20260824-101316_32932.jsonl"
BASELINE_114401 = LOGS / "perf_20260823-114401_15212.jsonl"

STEP_GATES: dict[str, list[str]] = {
    "FZ27-A": [
        "apply_binding_countcap_pct",
        "relight_apply_n_steady",
        "unit_apply_install_ms",
        "sim_steady_med",
    ],
    "FZ27-B": [
        "unit_apply_light_ms",
        "unit_apply_install_ms",
        "sim_steady_med",
    ],
    "FZ27-C": ["apply_util_steady", "sim_steady_med"],
    "FZ27-D": ["VB_steady_med", "vb_blink_steady", "opaque_refs_steady"],
    "FZ27-E": ["stream_steady_med", "phase_budget_over_pct", "sim_steady_med"],
    "FZ27-F": ["enter_wall_p90"],
    "FZ27-G": ["ALL"],
}

STEP_SCENARIO: dict[str, str] = {
    "FZ27-A": "fz-manual-plateau",
    "FZ27-B": "fz-manual-long",
    "FZ27-C": "fz-manual-long",
    "FZ27-D": "fz-manual-long",
    "FZ27-E": "fz-manual-plateau",
    "FZ27-F": "fz-manual-parity",
    "FZ27-G": "fz-manual-long",
}

REMEDIATION: dict[str, str] = {
    "apply_binding_countcap_pct": "A: Classify/stop flags — CountCap when applied>=earned",
    "relight_apply_n_steady": "A: time_cap wins, do not force min_cap=3",
    "apply_util_steady": "C: match apply to completed queue after light<5",
    "unit_apply_light_ms": "B: split drain vs merge; skip unchanged packed walk",
    "unit_apply_install_ms": "kill-switch: install must stay slim",
    "sim_steady_med": "kill-switch: do not grow cruise slice",
    "opaque_refs_steady": "D: published VB + keep live GPU",
    "VB_steady_med": "D: hysteresis + reticket defer",
    "vb_blink_steady": "D: hide-until-lit must not drop live GPU nh<=4",
    "stream_steady_med": "E: coalesce far Dirty revisit, cut mesh_schedule",
    "phase_budget_over_pct": "E: cut mesh_schedule not apply slice",
    "enter_wall_p90": "F: enter ticket seed without cruise producer tax",
}

MUST_NOT_REGRESS = {
    "black_sticky": ("sum", 0, "le"),
    "enter_wall_p90": ("p90", 280, "lt"),
    "unit_apply_install_ms": ("unit_med", 8, "lt"),
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
    ap.add_argument("--baseline-manual", default=str(BASELINE_FZ27))
    ap.add_argument("--build", action="store_true")
    ap.add_argument("--skip-autofly", action="store_true")
    ap.add_argument("--skip-build", action="store_true")
    ap.add_argument("--perf", default=None, help="perf jsonl when --skip-autofly")
    args = ap.parse_args()

    step = args.step if args.step.startswith("FZ27") else f"FZ27-{args.step}"
    scenario = args.scenario or STEP_SCENARIO.get(step, "fz-manual-long")
    gate_names = STEP_GATES.get(step, STEP_GATES["FZ27-G"])

    if args.build and not args.skip_build:
        rc = run(
            [
                "cmake",
                "--build",
                "bin",
                "--config",
                "Release",
                "--target",
                "Cubatarium",
                "miss_first_mesh_class_test",
                "mark_relit_integration_test",
            ]
        )
        if rc != 0:
            return rc
        for exe in ("miss_first_mesh_class_test.exe", "mark_relit_integration_test.exe"):
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
        fly = [
            sys.executable,
            str(ROOT / "tools" / "flight_sim_run.py"),
            "--scenario",
            scenario,
            "--phase-id",
            step,
            "--report",
            str(report),
        ]
        if scenario == "fz-manual-long":
            fly.extend(["--fly-phase-sec", "480", "--stop-phase-sec", "120"])
        rc = run(fly)
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

    baseline = Path(args.baseline_manual)
    delta = BIN / "tmp_fz26_delta.py"
    if baseline.is_file() and delta.is_file():
        subprocess.run(
            [sys.executable, str(delta), str(baseline), str(perf_path)],
            cwd=ROOT,
        )
    if step == "FZ27-G" and BASELINE_114401.is_file() and delta.is_file():
        subprocess.run(
            [sys.executable, str(delta), str(BASELINE_114401), str(perf_path)],
            cwd=ROOT,
        )

    gates = parse_gates(gate_proc.stdout)
    print(f"=== FZ27 step gates: {step} ===")
    track_fail = False
    if "ALL" in gate_names:
        check = [k for k, v in gates.items() if v == "FAIL"]
        for g in check:
            print(f"  FAIL: {g}")
            track_fail = True
    else:
        for g in gate_names:
            st = gates.get(g, "MISSING")
            print(f"  {g}: {st}")
            if st == "FAIL" or st == "MISSING":
                track_fail = True
                hint = REMEDIATION.get(g, "see plan track owner")
                print(f"    REMEDIATION: {hint}")

    for gname, (_kind, limit, _cmp) in MUST_NOT_REGRESS.items():
        st = gates.get(gname, "MISSING")
        print(f"  MUST_NOT_REGRESS {gname}: {st} (limit {limit})")
        if st == "FAIL":
            track_fail = True

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
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
