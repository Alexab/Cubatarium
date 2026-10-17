#!/usr/bin/env python3
"""SRBR P2: orphan/miss correlation + stream cost waterfall on 112418 stand."""
from __future__ import annotations

import json
import statistics as st
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
LOG = ROOT / "bin" / "logs" / "perf_20260826-112418_14164.jsonl"
OUT = Path(__file__).resolve().parents[1] / "raw" / "orphan_miss_cost.txt"


def load(p):
    rows = []
    for line in Path(p).read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("{"):
            r = json.loads(line)
            if r.get("kind") == "spike":
                rows.append(r)
    return rows


def med(xs):
    xs = [float(x) for x in xs if x is not None]
    return round(st.median(xs), 3) if xs else None


def main():
    u = load(LOG)
    # long stand at (-32,74) from spike 229
    stand = u[229:]
    lines = []
    lines.append(f"spikes={len(u)} stand={len(stand)}")

    # orphan vs miss correlation on late stand
    late = stand[-200:]
    orphan = [int(r.get("mesh_dirty_schedule_skip_orphan_n") or 0) for r in late]
    miss = [int(r.get("focus_missing_mesh") or 0) for r in late]
    ok = [int(r.get("mesh_dirty_schedule_ok_n") or 0) for r in late]
    fm = [int(r.get("dirty_fm_n") or 0) for r in late]
    lines.append(
        f"LATE orphan med={med(orphan)} ok med={med(ok)} fm med={med(fm)} miss_frac={sum(miss)/len(miss):.3f}"
    )
    high_orphan = [r for r in late if int(r.get("mesh_dirty_schedule_skip_orphan_n") or 0) >= 50]
    lines.append(f"late frames orphan>=50: {len(high_orphan)}/{len(late)}")

    # miss coords
    c = Counter()
    for r in stand:
        if int(r.get("focus_missing_mesh") or 0):
            c[(r.get("miss_cx"), r.get("miss_cy"), r.get("miss_cz"), r.get("miss_horiz"))] += 1
    lines.append(f"miss top: {c.most_common(8)}")

    # HasChunk orphan definition: Dirty entry without resident chunk
    # correlate underfeet_need with miss nh=0
    need_nh0 = sum(
        1
        for r in stand
        if int(r.get("underfeet_need") or 0)
        and int(r.get("miss_horiz") or -1) == 0
        and int(r.get("focus_missing_mesh") or 0)
    )
    lines.append(f"stand underfeet_need+miss_nh0: {need_nh0}/{len(stand)}")

    # cost waterfall late stand
    keys = [
        "wall_ms",
        "stream_ms",
        "mesh_emerge_ms",
        "mesh_dirty_tick_ms",
        "mesh_dirty_schedule_ms",
        "mesh_dirty_gpu_ms",
        "relight_drain_ms",
        "softdefer_empty_scan_ms",
        "world_streaming_phase_ms",
        "scene_ms",
    ]
    lines.append("LATE cost waterfall med:")
    for k in keys:
        lines.append(f"  {k:28s} {med([r.get(k) for r in late])}")

    # early/mid/late evolution of skip_orphan
    for lab, sl in [
        ("early", stand[:200]),
        ("mid", stand[2000:2200]),
        ("late", late),
    ]:
        lines.append(
            f"{lab}: orphan={med([r.get('mesh_dirty_schedule_skip_orphan_n') for r in sl])} "
            f"ok={med([r.get('mesh_dirty_schedule_ok_n') for r in sl])} "
            f"unf={med([r.get('unfinished_visual') for r in sl])} "
            f"vb={med([r.get('visible_black_focus_n') for r in sl])} "
            f"stream={med([r.get('stream_ms') for r in sl])} "
            f"dirty_n={med([r.get('dirty_n') for r in sl])}"
        )

    # pending_cols frozen?
    p0 = stand[200].get("pending_cols")
    p1 = stand[-1].get("pending_cols")
    lines.append(f"pending mid: {p0}")
    lines.append(f"pending end: {p1}")
    lines.append(f"pending frozen equal: {p0 == p1}")

    # skip breakdown late
    skips = [
        "mesh_dirty_schedule_skip_pipeline_n",
        "mesh_dirty_schedule_skip_snapshot_n",
        "mesh_dirty_schedule_skip_softdefer_n",
        "mesh_dirty_schedule_skip_locked_n",
        "mesh_dirty_schedule_skip_orphan_n",
        "mesh_dirty_schedule_skip_remesh_starve_n",
        "mesh_dirty_schedule_skip_other_n",
    ]
    lines.append("LATE skip med:")
    for k in skips:
        lines.append(f"  {k}: {med([r.get(k) for r in late])}")

    text = "\n".join(lines)
    OUT.write_text(text, encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()
