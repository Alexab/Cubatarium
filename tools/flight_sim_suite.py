#!/usr/bin/env python3
"""Run all manual-derived flight scenarios, collect gate reports, print summary.

Usage:
    python tools/flight_sim_suite.py                  # all scenarios, no build
    python tools/flight_sim_suite.py --build          # rebuild before first run
    python tools/flight_sim_suite.py --only land-cruise ocean-cruise
    python tools/flight_sim_suite.py --phase-id Era50  # label in history
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "bin"
RUN = ROOT / "tools" / "flight_sim_run.py"
REPORTS_DIR = BIN / "suite_reports"

# Each entry: (scenario_name, extra_args_list, description)
SCENARIOS: list[tuple[str, list[str], str]] = [
    (
        "land-cruise",
        ["--scenario", "land-cruise"],
        "Inland south corridor (-485,50) yaw 90, fly+stop — manual 084551/142306",
    ),
    (
        "land-stand",
        ["--scenario", "land-stand"],
        "Short east fly then stand >=60s — manual 170154 forever-hole",
    ),
    (
        "land-south",
        ["--scenario", "land-south"],
        "South -Z corridor (-483,54) yaw 270, fly+stop >=60s — manual 190350",
    ),
    (
        "land-south-short",
        ["--scenario", "land-south-short"],
        "Same -Z corridor, short stop ~10s — mid-heal snapshot",
    ),
    (
        "ocean-cruise",
        ["--scenario", "ocean-cruise"],
        "Ocean west cruise (-550,110) yaw 180 — manual 024756/122032/153653",
    ),
    (
        "idle-clean",
        ["--scenario", "idle-clean"],
        "Clean idle: land -Z corridor, short fly, long stand",
    ),
    (
        "fly-clean",
        ["--scenario", "fly-clean"],
        "Moving cruise stress: fly >=40s, judge move-segment sync/wall",
    ),
    (
        "fz-validate",
        ["--scenario", "fz-validate"],
        "FZ2.2 teleport smoke: land-south corridor (not DoD)",
    ),
    (
        "fz-manual-parity",
        ["--scenario", "fz-manual-parity"],
        "FZ2.3 DoD: World_164 resume, no teleport, yaw 270, idle45+fly90+stop90",
    ),
    (
        "fz-cold-enter",
        ["--scenario", "fz-cold-enter"],
        "FZ2.3 PL enter: cold load, no teleport, idle45+fly90+stop90",
    ),
    (
        "fz-manual-plateau",
        ["--scenario", "fz-manual-plateau"],
        "FZ2.4 DoD: resume no-teleport, idle45+fly45+stop15 (~105s plateau window)",
    ),
    (
        "fz-manual-long",
        ["--scenario", "fz-manual-long"],
        "FZ2.4 C8 proxy: resume no-teleport, idle45+fly100+stop120 (≥270s)",
    ),
    (
        "fz-ne-frontier-stand",
        ["--scenario", "fz-ne-frontier-stand"],
        "P14 SoftDefer repro: World_164 cold ~(118,86), idle30+fly0+stop120",
    ),
    (
        "fz-frontier-stand-resume",
        ["--scenario", "fz-frontier-stand-resume"],
        "SoftDefer standstill isolate: resume near frontier, idle15+fly10+stop90",
    ),
    (
        "fz-inring-cruise",
        ["--scenario", "fz-inring-cruise"],
        "P17 in-ring cruise: teleport (118,86) yaw180, idle15+fly35+stop120 vs 100413",
    ),
]


def run_scenario(
    scenario: str,
    extra_args: list[str],
    *,
    world: str,
    build: bool,
    build_dir: Path,
    phase_id: str,
    report_path: Path,
) -> dict:
    cmd = [
        sys.executable,
        str(RUN),
        "--world", world,
        *extra_args,
        "--report", str(report_path),
        "--build-dir", str(build_dir),
        "--phase-id", f"{phase_id}/{scenario}" if phase_id else scenario,
        "--update-best",
    ]
    if build:
        cmd.append("--build")

    t0 = time.time()
    print(f"\n{'='*60}", flush=True)
    print(f"  {scenario}", flush=True)
    print(f"{'='*60}", flush=True)
    rc = subprocess.call(cmd)
    elapsed = time.time() - t0

    result: dict = {
        "scenario": scenario,
        "rc": rc,
        "elapsed_sec": round(elapsed, 1),
    }
    if report_path.is_file():
        try:
            data = json.loads(report_path.read_text(encoding="utf-8"))
            result["pass"] = data.get("pass")
            result["hang_killed"] = data.get("hang_killed")
            m = data.get("metrics") or {}
            result["metrics"] = {
                k: m.get(k)
                for k in (
                    "wall_ms_med",
                    "wall_ms_fly_med",
                    "pending_light_focus_med",
                    "effective_holes_rate",
                    "post_stop_effective_holes_rate",
                    "effective_holes_blink_rate",
                    "post_stop_effective_holes_blink_rate",
                    "visible_black_blink_rate",
                    "black_sticky_blink_rate",
                    "visual_instability_blink_rate",
                    "relight_capture_partial_rate",
                    "pending_partial_capture_sec",
                    "chunks_traveled",
                    "post_stop_black_sticky_max",
                    "stop_wall_med",
                    "calm_stop_wall_med",
                )
            }
            g = data.get("gates") or {}
            gp = sum(1 for v in g.values() if v)
            result["gates_pass"] = f"{gp}/{len(g)}"
        except (json.JSONDecodeError, OSError):
            result["error"] = "report parse failed"
    else:
        result["error"] = "no report"

    return result


def print_summary(results: list[dict]) -> None:
    print(f"\n{'='*72}", flush=True)
    print("  SUITE SUMMARY", flush=True)
    print(f"{'='*72}", flush=True)

    hdr = (
        f"{'Scenario':<22s} {'Pass':>6s} {'Gates':>7s} {'Wall':>7s} "
        f"{'PL med':>7s} {'EH%':>6s} {'VB%':>6s} {'BS%':>6s} {'Inst%':>6s} "
        f"{'Chunks':>7s} {'Time':>7s}"
    )
    print(hdr, flush=True)
    print("-" * len(hdr), flush=True)

    for r in results:
        m = r.get("metrics") or {}
        status = "PASS" if r.get("pass") else ("HANG" if r.get("hang_killed") else "FAIL")
        wall = f"{m['wall_ms_med']:.0f}" if m.get("wall_ms_med") is not None else "—"
        pl = f"{m['pending_light_focus_med']:.0f}" if m.get("pending_light_focus_med") is not None else "—"
        eh = (
            f"{100.0 * m['effective_holes_rate']:.0f}"
            if m.get("effective_holes_rate") is not None
            else "—"
        )
        vb = (
            f"{100.0 * m['visible_black_blink_rate']:.0f}"
            if m.get("visible_black_blink_rate") is not None
            else "—"
        )
        bs = (
            f"{100.0 * m['black_sticky_blink_rate']:.0f}"
            if m.get("black_sticky_blink_rate") is not None
            else "—"
        )
        inst = (
            f"{100.0 * m['visual_instability_blink_rate']:.0f}"
            if m.get("visual_instability_blink_rate") is not None
            else "—"
        )
        ch = f"{m['chunks_traveled']:.0f}" if m.get("chunks_traveled") is not None else "—"
        gates = r.get("gates_pass", "—")
        t = f"{r['elapsed_sec']:.0f}s"
        print(
            f"{r['scenario']:<22s} {status:>6s} {gates:>7s} {wall:>7s} "
            f"{pl:>7s} {eh:>6s} {vb:>6s} {bs:>6s} {inst:>6s} {ch:>7s} {t:>7s}",
            flush=True,
        )

    total_time = sum(r.get("elapsed_sec", 0) for r in results)
    passed = sum(1 for r in results if r.get("pass"))
    print(f"\nTotal: {passed}/{len(results)} passed, {total_time:.0f}s elapsed", flush=True)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--world", default="World_164")
    ap.add_argument("--build", action="store_true", help="rebuild before first scenario")
    ap.add_argument(
        "--build-dir",
        type=Path,
        default=ROOT / "build" / ("desktop-msvc" if sys.platform == "win32" else "desktop-linux"),
    )
    ap.add_argument("--phase-id", default="", help="label prefix for history")
    ap.add_argument(
        "--only",
        nargs="*",
        default=None,
        help="run only these scenarios (space-separated names)",
    )
    ap.add_argument(
        "--skip",
        nargs="*",
        default=None,
        help="skip these scenarios",
    )
    args = ap.parse_args()

    REPORTS_DIR.mkdir(parents=True, exist_ok=True)

    scenarios = SCENARIOS
    if args.only:
        allowed = set(args.only)
        scenarios = [s for s in scenarios if s[0] in allowed]
    if args.skip:
        skip = set(args.skip)
        scenarios = [s for s in scenarios if s[0] not in skip]

    if not scenarios:
        print("No scenarios selected.", file=sys.stderr)
        return 1

    ts = time.strftime("%Y%m%d-%H%M%S")
    results: list[dict] = []

    for i, (name, extra, desc) in enumerate(scenarios):
        report = REPORTS_DIR / f"{ts}_{name}.json"
        build_this = args.build and i == 0
        r = run_scenario(
            name,
            extra,
            world=args.world,
            build=build_this,
            build_dir=args.build_dir,
            phase_id=args.phase_id,
            report_path=report,
        )
        results.append(r)

    print_summary(results)

    agg_path = REPORTS_DIR / f"{ts}_suite_summary.json"
    agg_path.write_text(json.dumps(results, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"\nSuite report: {agg_path}", flush=True)

    return 0 if all(r.get("pass") for r in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
