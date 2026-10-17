#!/usr/bin/env python3
import json
import statistics as st
from pathlib import Path


def load(path):
    rows = []
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.strip():
            continue
        try:
            rows.append(json.loads(line))
        except json.JSONDecodeError:
            pass
    return rows


def p90(vals):
    if not vals:
        return 0.0
    s = sorted(vals)
    return s[max(0, int(0.9 * len(s)) - 1)]


def seg_stats(periods, name):
    if not periods:
        print(name, "empty")
        return
    wall = [r.get("wall_ms") or 0 for r in periods]
    on = [r.get("opaque_cmd_on") or 0 for r in periods]
    holes = [r.get("unfinished_visual") or 0 for r in periods]
    void = [r.get("dark_face_void_near_n") or 0 for r in periods]
    imm = [r.get("mesh_immediate_count") or 0 for r in periods]
    print(
        name,
        f"n={len(periods)}",
        f"wall {st.median(wall):.1f}/{p90(wall):.1f}",
        f"opaque med {st.median(on):.0f}",
        f"holes med {st.median(holes):.0f}",
        f"void_max {max(void)}",
        f"imm_sum {sum(imm)}",
        f"miss {sum(1 for r in periods if r.get('focus_missing_mesh'))}",
    )


def main():
    manual = Path(r"E:/Work/Home/Cubatarium/bin/logs/perf_20260818-210623_30948.jsonl")
    autofly = Path(r"E:/Work/Home/Cubatarium/bin/logs/perf_20260818-200928_30372.jsonl")
    spawn = Path(r"E:/Work/Home/Cubatarium/bin/logs/perf_20260818-205340_27764.jsonl")

    periods = [r for r in load(manual) if r.get("kind") == "period"]
    print("manual periods", len(periods))
    for i, r in enumerate(periods):
        print(
            f"p{i:02d} cz={r.get('focus_cz')} cx={r.get('focus_cx')} "
            f"y={r.get('player_y'):.0f} on={r.get('opaque_cmd_on')} "
            f"holes={r.get('unfinished_visual')} miss={r.get('focus_missing_mesh')} "
            f"void={r.get('dark_face_void_near_n')} wall={r.get('wall_ms'):.0f} "
            f"imm={r.get('mesh_immediate_count')}"
        )

    print("\n--- void spikes ---")
    for i, r in enumerate(periods):
        if (r.get("dark_face_void_near_n") or 0) > 100:
            print(
                f"p{i:02d} cx={r.get('focus_cx')} cz={r.get('focus_cz')} "
                f"void={r.get('dark_face_void_near_n')} on={r.get('opaque_cmd_on')}"
            )

    per2 = [r for r in load(autofly) if r.get("kind") == "period"]
    same = [r for r in per2 if 50 <= (r.get("focus_cz") or -999) <= 53]
    print()
    seg_stats(periods, "manual_all")
    seg_stats([r for r in periods if r.get("focus_cz") == 50], "manual_linger50")
    seg_stats([r for r in periods if (r.get("focus_cz") or 0) >= 51], "manual_move51+")
    seg_stats(same, "autofly_cz50-53")

    # autofly full corridor stats
    seg_stats(per2, "autofly_all")
    seg_stats([r for r in load(spawn) if r.get("kind") == "period"], "spawn_manual_all")


if __name__ == "__main__":
    main()
