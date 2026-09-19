#!/usr/bin/env python3
"""Bake-off unload/keep amortize modes under product-174657-dive.

Writes bin/streaming_tune.json per mode, runs AF cold dive, analyzes, ranks.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "bin"
TUNE = BIN / "streaming_tune.json"
FLIGHT = ROOT / "tools" / "flight_sim_run.py"
ANALYZE = ROOT / "tools" / "analyze_stop_hang_dive.py"
OUT = BIN / "suite_reports" / "stop_hang_dive"


def write_tune(unload: int, keep: int) -> None:
    data = {}
    if TUNE.is_file():
        try:
            data = json.loads(TUNE.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            data = {}
    data["unload_amortize_mode"] = int(unload)
    data["keep_shell_amortize_mode"] = int(keep)
    TUNE.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {TUNE} unload={unload} keep={keep}", flush=True)


def run_dive(label: str, report: Path) -> Path | None:
    cmd = [
        sys.executable,
        str(FLIGHT),
        "--scenario",
        "product-174657-dive",
        "--report",
        str(report),
        "--build-dir",
        str(ROOT / "build" / "desktop-msvc"),
    ]
    print("running:", " ".join(cmd), flush=True)
    rc = subprocess.call(cmd, cwd=str(ROOT))
    print(f"{label} flight rc={rc}", flush=True)
    if not report.is_file():
        return None
    try:
        r = json.loads(report.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    for key in ("perf_jsonl", "perf_path", "perf"):
        p = r.get(key)
        if p and Path(p).is_file():
            return Path(p)
    ann = r.get("annotations") or {}
    for key in ("perf_jsonl", "perf_path", "perf"):
        p = ann.get(key)
        if p and Path(p).is_file():
            return Path(p)
    # phase history last line
    hist = BIN / "flight_sim_phase_history.jsonl"
    if hist.is_file():
        try:
            last = hist.read_text(encoding="utf-8").strip().splitlines()[-1]
            h = json.loads(last)
            p = h.get("perf")
            if p and Path(p).is_file():
                return Path(p)
        except (OSError, json.JSONDecodeError, IndexError):
            pass
    return None


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--phase", choices=["u", "k", "baseline"], required=True)
    ap.add_argument("--unload-fixed", type=int, default=0)
    ap.add_argument("--keep-fixed", type=int, default=0)
    ap.add_argument("--repeats", type=int, default=2)
    ap.add_argument("--modes", default="0,1,2,3,4")
    args = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)

    modes = [int(x) for x in args.modes.split(",") if x.strip() != ""]
    results_meta = []

    if args.phase == "baseline":
        write_tune(0, 0)
        report = OUT / "baseline_dive_report.json"
        perf = run_dive("U0K0", report)
        if not perf:
            print("FAIL: no perf for baseline", file=sys.stderr)
            return 2
        out = OUT / "stop_hang_baseline.json"
        subprocess.check_call(
            [
                sys.executable,
                str(ANALYZE),
                str(perf),
                "--label",
                "U0K0_baseline",
                "--report",
                str(report),
                "--baseline",
                str(out),
            ]
        )
        print(f"baseline written {out}")
        return 0

    for mode in modes:
        unload = mode if args.phase == "u" else args.unload_fixed
        keep = mode if args.phase == "k" else args.keep_fixed
        label = f"{'U' if args.phase == 'u' else 'K'}{mode}"
        perfs = []
        reports = []
        for rep in range(1, args.repeats + 1):
            write_tune(unload, keep)
            report = OUT / f"{label}_r{rep}_report.json"
            perf = run_dive(f"{label}_r{rep}", report)
            if perf:
                perfs.append(perf)
                reports.append(report)
                # copy perf aside
                dest = OUT / f"{label}_r{rep}.jsonl"
                dest.write_bytes(perf.read_bytes())
                perfs[-1] = dest
        if not perfs:
            print(f"WARN: no perfs for {label}", flush=True)
            continue
        # Analyze all reps for this mode (use first for ranking table entry;
        # also emit mean-ish via multi)
        cmd = [sys.executable, str(ANALYZE)]
        labels = []
        for i, p in enumerate(perfs):
            cmd.append(str(p))
            labels.append(f"{label}_r{i+1}")
        for lab in labels:
            cmd.extend(["--label", lab])
        for rpath in reports:
            cmd.extend(["--report", str(rpath)])
        cmd.extend(["--out", str(OUT / f"{label}_analyze.json")])
        subprocess.call(cmd)
        results_meta.append({"label": label, "unload": unload, "keep": keep, "perfs": [str(p) for p in perfs]})

    # Final rank across last analyze outs
    all_perfs = []
    all_labels = []
    all_reports = []
    for m in results_meta:
        for i, p in enumerate(m["perfs"]):
            all_perfs.append(p)
            all_labels.append(f"{m['label']}_r{i+1}")
            rp = OUT / f"{m['label']}_r{i+1}_report.json"
            all_reports.append(str(rp) if rp.is_file() else "")
    if all_perfs:
        cmd = [sys.executable, str(ANALYZE)] + all_perfs
        for lab in all_labels:
            cmd.extend(["--label", lab])
        for rp in all_reports:
            if rp:
                cmd.extend(["--report", rp])
        cmd.extend(["--out", str(OUT / f"bakeoff_{args.phase}_rank.json")])
        subprocess.call(cmd)

    (OUT / f"bakeoff_{args.phase}_meta.json").write_text(
        json.dumps(results_meta, indent=2) + "\n", encoding="utf-8"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
