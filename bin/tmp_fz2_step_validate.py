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
BUILD = ROOT / "build" / "desktop-msvc" / "Release"
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


def seg_by_time(rows: list[dict], t0: int, t1: int) -> list[dict]:
    return rows[t0 // 2 : t1 // 2]


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
    short_flight = n < 60
    enter_0_60 = seg_by_time(u, 0, 60)
    mid_60_120 = seg_by_time(u, 60, 120)
    steady_120_plus = seg_by_time(u, 120, 99999)
    enter = enter_0_60
    steady = steady_120_plus if len(steady_120_plus) >= 3 else (
        u[60:] if n > 90 else u[n // 2 :]
    )
    finalize = col(u, "relight_capture_finalize")
    apply_n = col(u, "relight_apply_n")
    finalize_apply_pairs = [
        (bool(f), int(a or 0))
        for f, a in zip(finalize, apply_n)
        if f is not None
    ]
    finalize_with_apply = sum(
        1 for f, a in finalize_apply_pairs if f and a > 0
    )
    finalize_count = sum(1 for f, _ in finalize_apply_pairs if f)
    finalize_apply_ratio = (
        finalize_with_apply / max(1, finalize_count) if finalize_count else 0.0
    )
    finalize_rate = (
        sum(1 for x in finalize if x) / max(1, len(finalize)) if finalize else 0.0
    )
    last = u[-1] if u else {}
    return {
        "spikes": n,
        "short_flight": short_flight,
        "PL_enter_med": med(col(enter, "pending_light_focus")),
        "PL_mid_med": med(col(mid_60_120, "pending_light_focus")),
        "PL_steady_med": med(col(steady, "pending_light_focus")),
        "revisit_steady_med": med(col(steady, "dirty_revisit_same_n")),
        "revisit_mid_med": med(col(mid_60_120, "dirty_revisit_same_n")),
        "revisit_enter_med": med(col(enter, "dirty_revisit_same_n")),
        "stream_steady_med": med(col(steady, "stream_ms")),
        "VB_steady_med": med(col(steady, "visible_black_focus_n")),
        "VB_mid_med": med(col(mid_60_120, "visible_black_focus_n")),
        "no_ticket_peak": max((r.get("visible_black_no_ticket_n") or 0) for r in u),
        "enter_no_ticket_med": med(col(enter, "visible_black_no_ticket_n")),
        "enter_wall_p90": p90(col(enter, "wall_ms")),
        "enter_fluid_p90": p90(col(enter, "fluid_map_cpu_ms")),
        "uf_flips_rate": flip_rate(u, "underfeet_opaque_present"),
        "black_sticky": sum(1 for r in u if r.get("black_sticky_blink")),
        "vb_blink_rate": blink_rate(u, "visible_black_focus_n"),
        "no_ticket_blink_rate": blink_rate(u, "visible_black_no_ticket_n"),
        "finalize_rate": finalize_rate,
        "finalize_apply_ratio": finalize_apply_ratio,
        "relight_note_suppressed_plateau_n": last.get(
            "relight_note_suppressed_plateau_n"
        ),
        "relight_apply_plateau_boost_n": last.get("relight_apply_plateau_boost_n"),
        "relight_finalize_dedup_n": last.get("relight_finalize_dedup_n"),
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
    ap.add_argument("--step", required=True, help="e.g. D1, FZ23-D1")
    ap.add_argument(
        "--scenario",
        default="fz-manual-plateau",
        choices=[
            "fz-manual-parity",
            "fz-manual-plateau",
            "fz-manual-long",
            "fz-cold-enter",
            "fz-validate",
        ],
        help="autofly scenario (default: fz-manual-plateau no-teleport DoD)",
    )
    ap.add_argument(
        "--baseline",
        default=str(BIN / "logs" / "perf_20260822-215535_30124.jsonl"),
        help="Release autofly ship baseline (default: 215535 parity)",
    )
    ap.add_argument(
        "--baseline-manual",
        default=str(BIN / "logs" / "perf_20260823-093406_25440.jsonl"),
        help="manual resume baseline for plateau parity (default: 093406)",
    )
    ap.add_argument("--build", action="store_true")
    ap.add_argument("--skip-autofly", action="store_true")
    ap.add_argument("--skip-build", action="store_true")
    args = ap.parse_args()

    baseline_path = Path(args.baseline)
    baseline_manual_path = Path(args.baseline_manual)
    if not baseline_path.is_file():
        alt = BIN / "logs" / "perf_20260822-201207_20824.jsonl"
        if alt.is_file():
            print(f"baseline missing {baseline_path}; using {alt}", flush=True)
            baseline_path = alt
        else:
            print(f"baseline missing: {baseline_path}", file=sys.stderr)
            return 2
    manual_m: dict | None = None
    if baseline_manual_path.is_file():
        manual_m = metrics(baseline_manual_path)
    else:
        print(
            f"warning: baseline-manual missing: {baseline_manual_path}",
            flush=True,
        )

    if args.build and not args.skip_build:
        # Release → bin/Cubatarium.exe (Debug stays under build/*/Debug).
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
        report = BIN / "suite_reports" / f"{stamp}_{args.scenario}.json"
        report.parent.mkdir(parents=True, exist_ok=True)
        phase_id = (
            args.step
            if args.step.startswith("FZ24")
            or args.step.startswith("FZ23")
            or args.step.startswith("FZ22")
            else f"FZ24-{args.step}"
        )
        rc = run(
            [
                sys.executable,
                str(ROOT / "tools" / "flight_sim_run.py"),
                "--scenario",
                args.scenario,
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
        "revisit_mid_med",
        "finalize_rate",
        "finalize_apply_ratio",
        "stream_steady_med",
        "PL_enter_med",
        "PL_mid_med",
        "PL_steady_med",
        "relight_note_suppressed_plateau_n",
        "relight_apply_plateau_boost_n",
    ):
        b = base_m.get(k)
        c = cur_m.get(k)
        d = pct_delta(c, b) if isinstance(c, (int, float)) and isinstance(
            b, (int, float)
        ) else None
        ds = f" ({d:+.1f}%)" if d is not None else ""
        print(f"  {k}: {c}{ds} vs baseline {b}")
    if manual_m:
        print("=== Parity vs manual baseline ===")
        for k in ("PL_enter_med", "PL_mid_med", "revisit_enter_med", "revisit_mid_med"):
            m = manual_m.get(k)
            c = cur_m.get(k)
            d = pct_delta(c, m) if isinstance(c, (int, float)) and isinstance(
                m, (int, float)
            ) else None
            ds = f" ({d:+.1f}% vs manual)" if d is not None else ""
            print(f"  {k}: auto={c} manual={m}{ds}")
        pl_mid = cur_m.get("PL_mid_med")
        if pl_mid is not None and pl_mid >= 30:
            print(
                "  HINT: PL mid still open — P0-A/B before P0-C",
                flush=True,
            )
    if triggers:
        print(f"  suggested O-tracks: {', '.join(triggers)}")
    else:
        print("  suggested O-tracks: (none)")
    pl_enter = cur_m.get("PL_enter_med")
    if pl_enter is not None and pl_enter >= 25 and args.scenario not in (
        "fz-cold-enter",
        "fz-manual-plateau",
    ):
        print(
            "  HINT: PL enter still open — also run "
            "`--scenario fz-cold-enter`",
            flush=True,
        )
    if args.scenario == "fz-cold-enter" and cur_m.get("PL_mid_med", 0) or 0 >= 30:
        print(
            "  HINT: cold PASS but plateau FAIL — do not treat cold-enter as sufficient",
            flush=True,
        )

    record = {
        "ts": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "step": args.step,
        "scenario": args.scenario,
        "perf": str(perf_path),
        "baseline": str(baseline_path),
        "baseline_manual": str(baseline_manual_path) if manual_m else None,
        "baseline_manual_metrics": manual_m,
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
