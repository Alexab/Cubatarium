#!/usr/bin/env python3
"""Agent-executable Tracy / perf capture helper (perf-root P0).

Usage:
  python tools/perf_capture.py --scenario fly-heavy [--tracy] [--build]
  python tools/perf_capture.py --scenario land-stand --no-tracy

With --tracy: configures CUBATARIUM_ENABLE_TRACY=ON, builds Release, runs
flight-sim, and if tracy-capture is on PATH records a .tracy file then
exports CSV via tracy-csvexport.

Without Tracy tools: still runs flight_sim_run and writes a FramePerfMonitor
summary into bin/research_perf_root/ as ground-truth fallback.
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "bin"
BUILD = ROOT / "build" / "desktop-msvc"
RESEARCH = BIN / "research_perf_root"
CAPTURES = BIN / "perf_captures"


def run(cmd: list[str], **kw) -> int:
    print("+", " ".join(cmd), flush=True)
    return subprocess.call(cmd, cwd=str(ROOT), **kw)


def configure_tracy(enable: bool) -> None:
    args = [
        "cmake",
        "-S",
        str(ROOT),
        "-B",
        str(BUILD),
        f"-DCUBATARIUM_ENABLE_TRACY={'ON' if enable else 'OFF'}",
    ]
    # Re-use existing preset cache when present.
    if (BUILD / "CMakeCache.txt").exists():
        run(args)
    else:
        run(["cmake", "--preset", "windows-msvc"] + args[4:])


def build_release() -> int:
    return run(
        [
            "cmake",
            "--build",
            str(BUILD),
            "--config",
            "Release",
            "--parallel",
            "8",
            "--target",
            "Cubatarium",
        ]
    )


def summarize_jsonl(jsonl: Path, out_md: Path, label: str) -> None:
    import statistics as st

    periods = []
    spikes = []
    for line in jsonl.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            r = json.loads(line)
        except json.JSONDecodeError:
            continue
        k = r.get("kind")
        if k == "period":
            periods.append(r)
        elif k == "spike":
            spikes.append(r)

    def med(rows, key):
        vals = []
        for r in rows:
            v = r.get(key)
            try:
                vals.append(float(v))
            except (TypeError, ValueError):
                pass
        return round(st.median(vals), 3) if vals else None

    keys = [
        "wall_ms",
        "stream_ms",
        "mesh_emerge_ms",
        "prep_refresh_pressure_ms",
        "prep_refresh_gap_ms",
        "prep_refresh_self_ms",
        "mesh_emerge_prep_ms",
        "mesh_emerge_prep_other_ms",
        "mesh_emerge_prep_self_ms",
        "scene_ms",
        "scene_filter_ready_ms",
        "scene_opaque_draw_ms",
        "scene_depth_capture_ms",
        "scene_transparent_ms",
        "scene_overlays_ms",
        "scene_self_ms",
        "render_total_ms",
        "stream_loads",
        "visual_holes",
        "unfinished_visual",
    ]
    lines = [
        f"# Ground truth — {label}",
        "",
        f"Source: `{jsonl.name}`",
        f"periods={len(periods)} spikes={len(spikes)}",
        "",
        "| metric | period_med | spike_med |",
        "|---|---:|---:|",
    ]
    for k in keys:
        lines.append(f"| {k} | {med(periods, k)} | {med(spikes, k)} |")

    # Attribution ratios
    press = med(periods, "prep_refresh_pressure_ms") or 0
    self_ms = med(periods, "prep_refresh_self_ms")
    if self_ms is None:
        self_ms = med(periods, "prep_refresh_gap_ms") or 0
    scene = med(periods, "scene_ms") or 0
    scene_self = med(periods, "scene_self_ms") or 0
    lines += [
        "",
        "## Attribution ratios",
        "",
        f"- prep_refresh self/total = {round(self_ms / press, 3) if press else 'n/a'} (target ≤ 0.10)",
        f"- scene self/total = {round(scene_self / scene, 3) if scene else 'n/a'} (target ≤ 0.10)",
        "",
        "## Notes",
        "",
        "Tracy CSV (if captured) lives in `bin/perf_captures/`.",
        "Top hotspots from code audit (pre-Tracy): RefreshStreamingPressure self,",
        "DrawCubeGeometry IsChunkSliceRenderReady / opaque sort / depth copy,",
        "mesh_emerge_prep untimed schedule block.",
        "",
    ]
    out_md.parent.mkdir(parents=True, exist_ok=True)
    out_md.write_text("\n".join(lines), encoding="utf-8")
    print("wrote", out_md)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--scenario", default="fly-heavy",
                    help="fly-heavy | land-stand | fz-cold-enter | …")
    ap.add_argument("--tracy", action="store_true")
    ap.add_argument("--no-tracy", action="store_true")
    ap.add_argument("--build", action="store_true")
    ap.add_argument("--world", default="World_164")
    args = ap.parse_args()
    use_tracy = args.tracy and not args.no_tracy

    RESEARCH.mkdir(parents=True, exist_ok=True)
    CAPTURES.mkdir(parents=True, exist_ok=True)
    ts = datetime.now().strftime("%Y%m%d-%H%M%S")

    if args.build or use_tracy:
        if use_tracy:
            configure_tracy(True)
        if build_release() != 0:
            return 1

    # Launch optional tracy-capture
    tracy_proc = None
    tracy_out = CAPTURES / f"{ts}_{args.scenario}.tracy"
    tracy_bin = shutil.which("tracy-capture")
    if use_tracy and tracy_bin:
        tracy_proc = subprocess.Popen(
            [tracy_bin, "-o", str(tracy_out), "-f"],
            cwd=str(ROOT),
        )
        print("tracy-capture pid", tracy_proc.pid)

    # Run flight sim via existing harness
    report = BIN / f"perf_capture_{args.scenario}_{ts}.json"
    cmd = [
        sys.executable,
        str(ROOT / "tools" / "flight_sim_run.py"),
        "--scenario",
        args.scenario if args.scenario != "fly-heavy" else "fz-manual-long",
        "--world",
        args.world,
        "--report",
        str(report),
    ]
    if args.scenario == "fly-heavy":
        cmd = [
            sys.executable,
            str(ROOT / "tools" / "flight_sim_run.py"),
            "--replay-manual-fly-heavy",
            "--world",
            args.world,
            "--report",
            str(report),
        ]
    rc = run(cmd)

    if tracy_proc:
        tracy_proc.terminate()
        try:
            tracy_proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            tracy_proc.kill()
        csv_bin = shutil.which("tracy-csvexport")
        if csv_bin and tracy_out.exists():
            csv_out = CAPTURES / f"{ts}_{args.scenario}.csv"
            run([csv_bin, "-o", str(csv_out), str(tracy_out)])

    # Pick newest perf jsonl
    logs = sorted((BIN / "logs").glob("perf_*.jsonl"), key=lambda p: p.stat().st_mtime)
    if logs:
        summarize_jsonl(
            logs[-1],
            RESEARCH / f"00_ground_truth_{args.scenario}.md",
            args.scenario,
        )
        # Also refresh aggregate index
        index = RESEARCH / "00_ground_truth.md"
        index.write_text(
            f"# Perf ground truth index\n\n"
            f"Latest capture: `{logs[-1].name}` scenario={args.scenario}\n\n"
            f"See `00_ground_truth_{args.scenario}.md` for metrics.\n"
            f"Tracy enabled build: {use_tracy}\n"
            f"Tracy tools present: capture={bool(tracy_bin)}\n",
            encoding="utf-8",
        )
    return rc


if __name__ == "__main__":
    sys.exit(main())
