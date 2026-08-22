#!/usr/bin/env python3
"""FZ2.2 step validation: build, unit, autofly, gates, P-OPT forensics."""
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
BUILD = ROOT / "build" / "desktop-msvc" / "Debug"
RESULTS = BIN / "fz22_step_results.jsonl"

MUST_NOT_REGRESS = {
    "black_sticky": ("sum", 0, "le"),
    "enter_no_ticket_med": ("med", 30, "lt"),
    "enter_wall_p90": ("p90", 250, "lt"),
    "enter_fluid_p90": ("p90", 200, "lt"),
    "uf_flips_rate": ("flip_rate", 0.05, "lt"),
}


def run(cmd: list[str], *, cwd: Path | None = None) -> int:
    print("+", " ".join(str(c) for c in cmd), flush=True)
    return subprocess.call(cmd, cwd=cwd or ROOT)


def newest_perf(since: float) -> Path | None:
    cands = [
        p
        for p in LOGS.glob("perf_*.jsonl")
        if p.stat().st_mtime >= since - 2.0
    ]
    if not cands:
        cands = sorted(LOGS.glob("perf_*.jsonl"), key=lambda p: p.stat().st_mtime)
    return cands[-1] if cands else None


def load_spikes(path: Path) -> list[dict]:
    rows = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("{"):
            r = json.loads(line)
            if r.get("kind") == "spike":
                rows.append(r)
    return rows


def med(xs: list) -> float | None:
    xs = [float(x) for x in xs if x is not None]
    if not xs:
        return None
    xs.sort()
    return xs[len(xs) // 2]


def p90(xs: list) -> float | None:
    xs = sorted(float(x) for x in xs if x is not None)
    if not xs:
        return None
    return xs[min(len(xs) - 1, int(0.9 * len(xs)))]


def flip_rate(rows: list[dict], key: str) -> float:
    xs = [r.get(key) for r in rows]
    return sum(1 for i in range(1, len(xs)) if xs[i] != xs[i - 1]) / max(
        1, len(xs)
    )


def seg(rows: list[dict], end_s: int) -> list[dict]:
    return rows[: end_s // 2]


def col(rows: list[dict], k: str) -> list:
    return [r.get(k) for r in rows]


def blink_rate(rows: list[dict], key: str) -> float:
    xs = [int(x) for x in col(rows, key) if x is not None]
    flips = sum(1 for i in range(1, len(xs)) if xs[i] != xs[i - 1])
    return flips / max(1, len(xs))


def metrics(path: Path) -> dict:
    u = load_spikes(path)
    n = len(u)
    enter = seg(u, 60)
    steady = u[60:] if n > 90 else u[n // 2 :]
    finalize = col(u, "relight_capture_finalize")
    finalize_rate = (
        sum(1 for x in finalize if x) / max(1, len(finalize)) if finalize else 0.0
    )
    return {
        "spikes": n,
        "PL_enter_med": med(col(enter, "pending_light_focus")),
        "PL_steady_med": med(col(steady, "pending_light_focus")),
        "revisit_steady_med": med(col(steady, "dirty_revisit_same_n")),
        "revisit_enter_med": med(col(enter, "dirty_revisit_same_n")),
        "stream_steady_med": med(col(steady, "stream_ms")),
        "VB_steady_med": med(col(steady, "visible_black_focus_n")),
        "no_ticket_peak": max((r.get("visible_black_no_ticket_n") or 0) for r in u),
        "enter_no_ticket_med": med(col(enter, "visible_black_no_ticket_n")),
        "enter_wall_p90": p90(col(enter, "wall_ms")),
        "enter_fluid_p90": p90(col(enter, "fluid_map_cpu_ms")),
        "uf_flips_rate": flip_rate(u, "underfeet_opaque_present"),
        "black_sticky": sum(1 for r in u if r.get("black_sticky_blink")),
        "vb_blink_rate": blink_rate(u, "visible_black_focus_n"),
        "no_ticket_blink_rate": blink_rate(u, "visible_black_no_ticket_n"),
        "finalize_rate": finalize_rate,
    }


def parse_gate_check(text: str) -> dict[str, str]:
    out = {}
    for m in re.finditer(
        r"^\s+(\w+):\s+([\d.]+)\s+\(target<([^)]+)\)\s+(PASS|FAIL)",
        text,
        re.M,
    ):
        out[m.group(1)] = m.group(4)
    return out


def p_opt_triggers(cur: dict, base: dict) -> list[str]:
    triggers: list[str] = []
    pl_enter = cur.get("PL_enter_med") or 999
    pl_steady = cur.get("PL_steady_med") or 999
    if pl_enter > 30 and (base.get("PL_enter_med") or 0) > 30:
        triggers.append("O1")
    if cur.get("finalize_rate", 0) > 0.95 and pl_steady > 25:
        triggers.append("O2")
    if cur.get("vb_blink_rate", 0) > 0.25:
        triggers.append("O3")
    if (cur.get("revisit_steady_med") or 0) > 120 and (
        cur.get("stream_steady_med") or 0
    ) > 40:
        triggers.append("O4")
    if cur.get("no_ticket_peak", 999) >= 85 and (
        cur.get("no_ticket_blink_rate") or 0
    ) > 0.2:
        triggers.append("O5")
    if (cur.get("stream_steady_med") or 0) > 40:
        triggers.append("O6")
    return triggers


def pct_delta(cur: float | None, base: float | None) -> float | None:
    if cur is None or base is None or base == 0:
        return None
    return (cur - base) / base * 100.0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--step", required=True, help="e.g. C1a, FZ22-C1a")
    ap.add_argument(
        "--baseline",
        default=str(BIN / "logs" / "perf_20260822-184927_8256.jsonl"),
    )
    ap.add_argument("--build", action="store_true")
    ap.add_argument("--skip-autofly", action="store_true")
    ap.add_argument("--skip-build", action="store_true")
    args = ap.parse_args()

    baseline_path = Path(args.baseline)
    if not baseline_path.is_file():
        print(f"baseline missing: {baseline_path}", file=sys.stderr)
        return 2

    if args.build and not args.skip_build:
        rc = run(
            [
                "cmake",
                "--build",
                "build/desktop-msvc",
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
        report = BIN / "suite_reports" / f"{stamp}_fz-validate.json"
        report.parent.mkdir(parents=True, exist_ok=True)
        phase_id = args.step if args.step.startswith("FZ22") else f"FZ22-{args.step}"
        rc = run(
            [
                sys.executable,
                str(ROOT / "tools" / "flight_sim_run.py"),
                "--scenario",
                "fz-validate",
                "--phase-id",
                phase_id,
                "--report",
                str(report),
            ]
        )
        if rc not in (0, 1, 2):
            return rc
        perf_path = newest_perf(t0)
        if perf_path is None:
            print("FAIL: no perf jsonl after autofly", file=sys.stderr)
            return 3
        print(f"perf: {perf_path}", flush=True)
    else:
        perf_path = newest_perf(time.time())
        if perf_path is None:
            print("FAIL: no perf jsonl", file=sys.stderr)
            return 3

    gate_proc = subprocess.run(
        [sys.executable, str(BIN / "tmp_fz2_gate_check.py"), str(perf_path)],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    print(gate_proc.stdout, end="")
    if gate_proc.stderr:
        print(gate_proc.stderr, file=sys.stderr, end="")

    forensics_proc = subprocess.run(
        [sys.executable, str(BIN / "tmp_cold_pl_forensics.py"), str(perf_path)],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    print(forensics_proc.stdout, end="")

    base_m = metrics(baseline_path)
    cur_m = metrics(perf_path)
    triggers = p_opt_triggers(cur_m, base_m)

    print("=== P-OPT report ===")
    for k in (
        "vb_blink_rate",
        "no_ticket_blink_rate",
        "revisit_steady_med",
        "finalize_rate",
        "stream_steady_med",
        "PL_enter_med",
        "PL_steady_med",
    ):
        b = base_m.get(k)
        c = cur_m.get(k)
        d = pct_delta(c, b)
        ds = f" ({d:+.1f}%)" if d is not None else ""
        print(f"  {k}: {c}{ds} vs baseline {b}")
    if triggers:
        print(f"  suggested O-tracks: {', '.join(triggers)}")
    else:
        print("  suggested O-tracks: (none)")

    record = {
        "ts": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "step": args.step,
        "perf": str(perf_path),
        "baseline": str(baseline_path),
        "metrics": cur_m,
        "baseline_metrics": base_m,
        "gates": parse_gate_check(gate_proc.stdout),
        "p_opt_triggers": triggers,
    }
    RESULTS.parent.mkdir(parents=True, exist_ok=True)
    with RESULTS.open("a", encoding="utf-8") as f:
        f.write(json.dumps(record) + "\n")

    regress = False
    for gname, (_, limit, op) in MUST_NOT_REGRESS.items():
        key = gname
        val = cur_m.get(key)
        if val is None:
            continue
        if op == "lt" and val >= limit:
            print(f"MUST-NOT-REGRESS FAIL: {gname}={val} >= {limit}")
            regress = True
        if op == "le" and val > limit:
            print(f"MUST-NOT-REGRESS FAIL: {gname}={val} > {limit}")
            regress = True

    return 1 if regress else 0


if __name__ == "__main__":
    raise SystemExit(main())
