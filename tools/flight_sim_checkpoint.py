#!/usr/bin/env python3
"""Create git checkpoint commit when flight-sim metrics improved."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "bin"
BEST = BIN / "flight_sim_gate_report_west_best.json"


def load_json(path: Path) -> dict | None:
    if not path.is_file():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def gates_pass_count(result: dict) -> int:
    g = result.get("gates") or {}
    return sum(1 for v in g.values() if v)


def is_improvement(cur: dict, best: dict | None) -> bool:
    if gates_pass_count(cur) < 6:
        return False
    gs_cur = cur.get("gates_stop") or {}
    if sum(1 for v in gs_cur.values() if v) < 4:
        return False
    if best is None:
        return cur.get("pass", False)
    if gates_pass_count(cur) > gates_pass_count(best):
        return True
    if gates_pass_count(cur) < gates_pass_count(best):
        return False
    gs_cur = cur.get("gates_stop") or {}
    gs_best = best.get("gates_stop") or {}
    rsc = sum(1 for v in gs_cur.values() if v)
    bsc = sum(1 for v in gs_best.values() if v)
    if rsc > bsc:
        return True
    if rsc < bsc:
        return False
    cm = cur.get("metrics") or {}
    bm = best.get("metrics") or {}
    cp = cm.get("pending_light_focus_med")
    bp = bm.get("pending_light_focus_med")
    if cp is not None and bp is not None and cp < bp - 4.0:
        return True
    return False


def commit_message(label: str, result: dict) -> str:
    m = result.get("metrics") or {}
    g = result.get("gates") or {}
    passed = gates_pass_count(result)
    total = len(g)
    soft = result.get("soft") or {}
    lines = [
        f"perf(streaming): checkpoint {label} — west {passed}/{total} gates",
        "",
        f"holes_rate={m.get('holes_rate')}",
        f"pending_med={m.get('pending_light_focus_med')}",
        f"red_rate={m.get('red_rate')}",
        f"wall_med={m.get('wall_ms_med')}",
        f"black_proxy={soft.get('black_proxy_rate')}",
    ]
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--label", required=True)
    ap.add_argument("--report", type=Path, default=BIN / "flight_sim_gate_report_west_latest.json")
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args()

    cur = load_json(args.report)
    if cur is None:
        print(f"FAIL: missing report {args.report}", file=sys.stderr)
        return 1

    best = load_json(BEST)
    if not args.force and not is_improvement(cur, best):
        print("SKIP: no improvement vs west_best", flush=True)
        return 0

    if best is None or is_improvement(cur, best):
        BEST.parent.mkdir(parents=True, exist_ok=True)
        BEST.write_text(args.report.read_text(encoding="utf-8"), encoding="utf-8")

    st = subprocess.run(
        ["git", "status", "--porcelain"],
        cwd=str(ROOT),
        capture_output=True,
        text=True,
        check=True,
    )
    lines = [ln for ln in st.stdout.splitlines() if ln.strip()]
    src_changes = [ln for ln in lines if "src/" in ln or "tools/" in ln]
    if not src_changes:
        print("SKIP: no src/tools changes to commit", flush=True)
        return 0

    subprocess.run(["git", "add", "src", "tools"], cwd=str(ROOT), check=True)
    msg = commit_message(args.label, cur)
    rc = subprocess.run(
        ["git", "commit", "-m", msg],
        cwd=str(ROOT),
    ).returncode
    if rc == 0:
        tag = f"perf-checkpoint-{args.label}"
        subprocess.run(["git", "tag", "-f", tag], cwd=str(ROOT))
        print(f"committed + tagged {tag}", flush=True)
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
