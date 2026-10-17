#!/usr/bin/env python3
"""Audit idle/stand remesh livelock from perf jsonl."""
from __future__ import annotations

import json
import sys
from pathlib import Path
from statistics import median


def load_periods(path: Path) -> list[dict]:
    rows = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.strip():
            continue
        try:
            r = json.loads(line)
        except json.JSONDecodeError:
            continue
        if r.get("kind") == "period":
            rows.append(r)
    return rows


def main() -> int:
    path = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    per = load_periods(path)
    print(f"file={path.name} periods={len(per)}")
    if len(per) < 10:
        return 1
    seg = per[-60:] if len(per) >= 60 else per[len(per) // 2 :]
    keys = [
        "dirty_n",
        "pending_light_focus",
        "visible_black_focus_n",
        "black_sticky",
        "unfinished_visual",
        "focus_dirty",
        "mesh_async",
        "relight_fifo_n",
        "relight_apply_final_n",
        "mark_relit_skip_already_dirty_n",
        "mark_relit_skip_already_raa_n",
        "mark_relit_schedule_n",
        "sticky_erase_remesh_commit_n",
        "sticky_erase_drawable_n",
        "opaque_cmd_on",
        "chunk_meshed_unlit",
        "dark_face_stale_near_n",
        "dark_face_near_n",
        "softdefer_held_n",
        "mesh_dirty_schedule_ok_n",
        "mesh_dirty_gpu_n",
        "mesh_apply_stale_delta",
        "wall_ms",
        "mesh_emerge_ms",
        "focus_cx",
        "focus_cz",
    ]
    sample = seg[0]
    avail = [k for k in keys if k in sample]
    print("last_segment_n", len(seg))
    for k in avail:
        vals = [float(r.get(k) or 0) for r in seg]
        print(
            f"  {k}: med={median(vals):.1f} min={min(vals):.1f} "
            f"max={max(vals):.1f} sum={sum(vals):.0f}"
        )
    fcs = [(r.get("focus_cx"), r.get("focus_cz")) for r in seg]
    print("focus_unique", len(set(fcs)), "first", fcs[0], "last", fcs[-1])
    if "opaque_cmd_on" in sample:
        ons = [float(r.get("opaque_cmd_on") or 0) for r in seg]
        deltas = [abs(ons[i] - ons[i - 1]) for i in range(1, len(ons))]
        print(
            "opaque_on_absdelta med/max",
            median(deltas) if deltas else None,
            max(deltas) if deltas else None,
        )
    # Per-period timeline of key settle fields (every 5th)
    print("\n--- timeline (every 5th of last segment) ---")
    for i, r in enumerate(seg):
        if i % 5 != 0:
            continue
        print(
            f"{i:03d} pl={r.get('pending_light_focus')} vb={r.get('visible_black_focus_n')} "
            f"bs={r.get('black_sticky')} uf={r.get('unfinished_visual')} "
            f"dirty={r.get('dirty_n')} skd={r.get('mark_relit_skip_already_dirty_n')} "
            f"skraa={r.get('mark_relit_skip_already_raa_n')} af={r.get('relight_apply_final_n')} "
            f"unlit={r.get('chunk_meshed_unlit')} stale={r.get('dark_face_stale_near_n')} "
            f"fifo={r.get('relight_fifo_n')} fc=({r.get('focus_cx')},{r.get('focus_cz')})"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
