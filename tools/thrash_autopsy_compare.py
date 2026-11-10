#!/usr/bin/env python3
"""Offline thrash autopsy: mid-corridor 121131 vs 102527 (existing JSONL fields)."""
from __future__ import annotations

import json
import statistics
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def periods(path: Path) -> list[dict]:
    rows = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("{"):
            r = json.loads(line)
            if r.get("kind") == "period":
                rows.append(r)
    return rows


def mid_corridor(rows: list[dict]) -> list[dict]:
    n = len(rows)
    mid = rows[n // 3 : (2 * n) // 3] if n >= 3 else rows
    out = []
    for r in mid:
        try:
            if 2.0 <= float(r.get("focus_cx") or 0) <= 5.0:
                out.append(r)
        except (TypeError, ValueError):
            continue
    return out if out else mid


def med(xs: list[float]):
    return statistics.median(xs) if xs else None


def collect(rows: list[dict], key: str) -> list[float]:
    out = []
    for r in rows:
        try:
            if r.get(key) is not None:
                out.append(float(r[key]))
        except (TypeError, ValueError):
            continue
    return out


def summarize(label: str, path: Path) -> dict:
    rows = mid_corridor(periods(path))
    stale = collect(rows, "mesh_apply_stale_visual") or collect(rows, "mesh_apply_stale")
    progress = collect(rows, "publication_progress_unit_n")
    reorder = collect(rows, "transparent_cmd_reorder_n")
    reorder_wo_progress = 0
    for r in rows:
        try:
            if float(r.get("transparent_cmd_reorder_n") or 0) > 0 and float(
                r.get("publication_progress_unit_n") or 0
            ) == 0:
                reorder_wo_progress += 1
        except (TypeError, ValueError):
            pass
    return {
        "label": label,
        "rows": len(rows),
        "stale_visual_med": med(stale),
        "stale_visual_max": max(stale) if stale else None,
        "publication_progress_med": med(progress),
        "transparent_cmd_reorder_med": med(reorder),
        "periods_reorder_without_progress": reorder_wo_progress,
        "fraction_reorder_without_progress": (
            reorder_wo_progress / len(rows) if rows else None
        ),
        "dark_face_stale_near_med": med(collect(rows, "dark_face_stale_near_n")),
        "stalled_med": med(collect(rows, "visible_black_fully_dark_stalled_n")),
        "near_focus_holes_med": med(collect(rows, "near_focus_holes")),
        "note": "pubver_changed_without_fresh / pass_mesh_rev_lag require post-C1 exe",
    }


def main() -> int:
    a = summarize(
        "102527", ROOT / "bin/logs/perf_20260915-102527_35348.jsonl"
    )
    b = summarize(
        "121131", ROOT / "bin/logs/perf_20260915-121131_5256.jsonl"
    )
    decision = (
        "C1 (narrow publicationVersion to any_fresh): 121131 shows higher "
        "stale_visual and cmd_reorder with more publication_progress mid; "
        "holes counters remain 0 (missing-mesh telem). Default per plan A2.3."
    )
    out = {
        "compare": [a, b],
        "decision": decision,
        "c2": "skip until opaque_transparent_batch_mismatch telem on new flights",
    }
    md_path = ROOT / "bin/suite_reports/g1_a10_relight/thrash_autopsy_121131.md"
    md_path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# Thrash autopsy 121131 vs 102527",
        "",
        f"| Field | 102527 | 121131 |",
        f"|---|---:|---:|",
    ]
    keys = [
        "rows",
        "stale_visual_med",
        "stale_visual_max",
        "publication_progress_med",
        "transparent_cmd_reorder_med",
        "periods_reorder_without_progress",
        "fraction_reorder_without_progress",
        "dark_face_stale_near_med",
        "stalled_med",
        "near_focus_holes_med",
    ]
    for k in keys:
        lines.append(f"| `{k}` | {a.get(k)} | {b.get(k)} |")
    lines += ["", f"**Decision:** {decision}", "", "```json", json.dumps(out, indent=2), "```", ""]
    md_path.write_text("\n".join(lines), encoding="utf-8")
    print(md_path)
    print(json.dumps(out, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
