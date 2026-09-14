#!/usr/bin/env python3
"""Q9 world-acceptance harness: aggregate 3 cold + 3 warm F5 flights.

Does not run the game. After you collect six identical-route flights, pass their
perf/enter pairs (or a directory of matching logs) and this tool:

  1. Writes a manifest template (git sha, exe hash, seed/route placeholders)
  2. Runs CompareFlightF5-class checks per flight
  3. Emits a summary JSON with per-run + median/worst across the batch

Example (after 6 manual flights with matching route):

  python tools/q9_acceptance_suite.py \\
    --exe bin/Cubatarium.exe \\
    --flight bin/logs/perf_A.jsonl:bin/logs/enter_A.jsonl \\
    --flight bin/logs/perf_B.jsonl:bin/logs/enter_B.jsonl \\
    ... (6 total) \\
    --out bin/suite_reports/q9_summary.json \\
    --seed <seed> --route 174657-class --cold 3 --warm 3

Autofly proxy for the same west corridor (not north --replay-manual yaw 90):

  python tools/flight_sim_run.py --world World_164 --scenario product-174657
  # route JSON: tools/manual_flight_world164_product_174657.json
"""
from __future__ import annotations

import argparse
import hashlib
import json
import statistics
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from AnalyzeEnterLit import analyze_enter_lit  # noqa: E402


def git_sha() -> str:
    try:
        out = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True, stderr=subprocess.DEVNULL
        )
        return out.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return "unknown"


def exe_sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def parse_flight(spec: str) -> tuple[Path, Path | None]:
    if ":" in spec:
        perf_s, enter_s = spec.split(":", 1)
        enter = Path(enter_s) if enter_s else None
        return Path(perf_s), enter
    return Path(spec), None


def analyze_one(perf: Path, enter: Path | None) -> dict:
    report: dict = {
        "perf": str(perf),
        "enter_path": str(enter) if enter else None,
    }
    try:
        frames = []
        with perf.open(encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line:
                    frames.append(json.loads(line))
        n = len(frames)
        start = int(n * 0.2)
        cruise = frames[start:] if n else []

        def med(key: str):
            xs = [r.get(key) for r in cruise if isinstance(r.get(key), (int, float))]
            return statistics.median(xs) if xs else None

        report["metrics"] = {
            "wall_med": med("wall_ms"),
            "mesh_apply_stale_med": med("mesh_apply_stale"),
            "unfinished_visual_med": med("unfinished_visual"),
            "visual_holes_med": med("visual_holes"),
            "visible_black_focus_med": med("visible_black_focus_n"),
            "shadow_mismatch_med": med("column_record_shadow_mismatch_n"),
            "job_rr_med": med("column_job_render_ready_n"),
            "empty_backlog_med": med("empty_backlog"),
            "pool_fence_timeout_med": med("pool_fence_timeout"),
        }
        report["frame_count"] = n
    except Exception as e:  # noqa: BLE001
        report["error"] = str(e)

    if enter and enter.exists():
        try:
            report["enter"] = analyze_enter_lit(enter)
        except Exception as e:  # noqa: BLE001
            report["enter_error"] = str(e)
    return report


def drawable_ok(run: dict) -> bool:
    m = run.get("metrics") or {}
    stale = m.get("mesh_apply_stale_med")
    unf = m.get("unfinished_visual_med")
    holes = m.get("visual_holes_med")
    if stale is None:
        return False
    # vs product_anchor 192015 class: stale not mass, unfinished not growing, holes 0
    if unf is not None and unf > 5:
        return False
    if holes is not None and holes > 2:
        return False
    return True


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--exe", type=Path, default=ROOT / "bin" / "Cubatarium.exe")
    ap.add_argument(
        "--flight",
        action="append",
        default=[],
        help="perf.jsonl[:enter_lit.jsonl] — repeat for each of 6 flights",
    )
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--seed", default="")
    ap.add_argument("--route", default="174657-class")
    ap.add_argument("--cold", type=int, default=3)
    ap.add_argument("--warm", type=int, default=3)
    ap.add_argument("--tag", default="")
    args = ap.parse_args()

    if len(args.flight) == 0:
        ap.error("pass at least one --flight perf[:enter]; Q9 wants cold+warm = 6")

    expected = args.cold + args.warm
    runs = []
    for i, spec in enumerate(args.flight):
        perf, enter = parse_flight(spec)
        kind = "cold" if i < args.cold else "warm"
        if i >= expected:
            kind = "extra"
        one = analyze_one(perf, enter)
        one["kind"] = kind
        one["index"] = i
        one["drawable_ok"] = drawable_ok(one)
        runs.append(one)

    summary = {
        "schema": "q9_acceptance_v1",
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "manifest": {
            "git_sha": git_sha(),
            "exe_path": str(args.exe),
            "exe_sha256": exe_sha256(args.exe) if args.exe.exists() else None,
            "seed": args.seed or None,
            "route": args.route,
            "cold_n": args.cold,
            "warm_n": args.warm,
            "tag": args.tag or None,
            "notes": (
                "Fill seed/route/HUD/driver/resolution if omitted. "
                "Include stop/reverse/edits/unload/reload/normal shutdown in the "
                "six flights when possible. Do not change gates after seeing results."
            ),
        },
        "runs": runs,
        "batch": {
            "n": len(runs),
            "drawable_pass_n": sum(1 for r in runs if r.get("drawable_ok")),
            "all_drawable_ok": all(r.get("drawable_ok") for r in runs),
        },
    }

    # Median stale across drawable-ok runs
    stales = [
        r["metrics"]["mesh_apply_stale_med"]
        for r in runs
        if r.get("drawable_ok") and r.get("metrics", {}).get("mesh_apply_stale_med") is not None
    ]
    if stales:
        summary["batch"]["stale_median"] = statistics.median(stales)
        summary["batch"]["stale_worst"] = max(stales)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary["batch"], indent=2))
    print(f"wrote {args.out}")
    if len(runs) < expected:
        print(
            f"NOTE: got {len(runs)} flights, Q9 wants {expected} "
            f"({args.cold} cold + {args.warm} warm)",
            file=sys.stderr,
        )
        return 2
    return 0 if summary["batch"]["all_drawable_ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
