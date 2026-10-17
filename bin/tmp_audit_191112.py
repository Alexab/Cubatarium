#!/usr/bin/env python3
"""Quick triage of manual 191112 vs SoT 143831."""
from __future__ import annotations

import json
import statistics
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load_periods(path: Path) -> list[dict]:
    rows = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.startswith("{"):
            continue
        row = json.loads(line)
        if row.get("kind") == "period":
            rows.append(row)
    return rows


def med(vals: list[float]) -> float | None:
    return statistics.median(vals) if vals else None


def mx(vals: list[float]) -> float | None:
    return max(vals) if vals else None


def vals(rows: list[dict], key: str) -> list[float]:
    out = []
    for r in rows:
        v = r.get(key)
        if v is None:
            continue
        try:
            out.append(float(v))
        except (TypeError, ValueError):
            pass
    return out


def slice_fly(rows: list[dict]) -> list[dict]:
    fly = [r for r in rows if float(r.get("movement_speed") or 0) > 2.0]
    return fly if fly else rows


def summarize(label: str, path: Path) -> dict:
    rows = load_periods(path)
    fly = slice_fly(rows)
    last = rows[-max(1, len(rows) // 5) :] if rows else []
    keys = [
        "visible_black_focus",
        "visible_black_fully_dark",
        "visible_black_fully_dark_repair",
        "visible_black_fully_dark_stalled",
        "unfinished_visual",
        "column_loaded_no_mesh",
        "prior_lit_hold_n",
        "prior_lit_hold_age_max",
        "stale_accepted_refresh",
        "mesh_apply_stale_visual",
        "mesh_apply_stale_geom",
        "publication_material_block_id_flip_n",
        "pass_dual_backend_same_coord_n",
        "transparent_cmd_reorder_n",
        "dark_face_void_near",
        "dark_face_stale",
        "fog_pull_in_rd",
        "fog_pull_in_margin",
        "mark_relit_invoked_n",
        "markrelit_schedule_n",
        "gpu_kick_n",
        "ok_remesh_n",
        "focus_cx",
        "player_y",
    ]
    # tolerate alternate names
    aliases = {
        "visible_black_focus": ["visible_black_focus_n", "visible_black_focus"],
        "visible_black_fully_dark": ["visible_black_fully_dark_n", "visible_black_fully_dark"],
        "visible_black_fully_dark_repair": [
            "visible_black_fully_dark_repair_n",
            "visible_black_fully_dark_repair",
        ],
        "visible_black_fully_dark_stalled": [
            "visible_black_fully_dark_stalled_n",
            "visible_black_fully_dark_stalled",
        ],
        "prior_lit_hold_n": ["prior_lit_hold_n"],
        "dark_face_void_near": ["dark_face_void_near_n", "dark_face_void_near", "dark_face_void"],
        "dark_face_stale": ["dark_face_stale_near_n", "dark_face_stale"],
        "stale_accepted_refresh": [
            "mesh_apply_stale_accepted_refresh",
            "stale_accepted_refresh",
            "mesh_apply_accepted_refresh",
        ],
        "mark_relit_invoked_n": ["mark_relit_invoked_n", "markrelit_invoked_n"],
        "markrelit_schedule_n": ["markrelit_schedule_n", "mark_relit_schedule_n"],
        "gpu_kick_n": ["gpu_kick_n", "mesh_gpu_kick_n"],
        "ok_remesh_n": ["ok_remesh_n", "remesh_ok_n"],
    }

    def pick(subset: list[dict], name: str) -> list[float]:
        for k in aliases.get(name, [name]):
            v = vals(subset, k)
            if v:
                return v
        return vals(subset, name)

    out = {
        "label": label,
        "path": str(path),
        "periods": len(rows),
        "fly_n": len(fly),
        "focus_cx_min": mx([-x for x in pick(rows, "focus_cx")]) and min(pick(rows, "focus_cx") or [0]),
        "focus_cx_range": (
            (min(pick(rows, "focus_cx")), max(pick(rows, "focus_cx")))
            if pick(rows, "focus_cx")
            else None
        ),
    }
    for name in keys:
        f = pick(fly, name)
        t = pick(last, name)
        a = pick(rows, name)
        out[name] = {
            "fly_med": med(f),
            "fly_max": mx(f),
            "tail_med": med(t),
            "tail_max": mx(t),
            "all_med": med(a),
            "all_max": mx(a),
        }
    # timeline last 8
    tl = []
    for r in rows[-8:]:
        tl.append(
            {
                "cx": r.get("focus_cx"),
                "y": r.get("player_y"),
                "vb": r.get("visible_black_focus") or r.get("visible_black_focus_n"),
                "fd": r.get("visible_black_fully_dark")
                or r.get("visible_black_fully_dark_n"),
                "repair": r.get("visible_black_fully_dark_repair")
                or r.get("visible_black_fully_dark_repair_n"),
                "stall": r.get("visible_black_fully_dark_stalled")
                or r.get("visible_black_fully_dark_stalled_n"),
                "unf": r.get("unfinished_visual"),
                "nlm": r.get("column_loaded_no_mesh"),
                "plh": r.get("prior_lit_hold_n"),
                "flip": r.get("publication_material_block_id_flip_n"),
                "dual": r.get("pass_dual_backend_same_coord_n"),
                "tcr": r.get("transparent_cmd_reorder_n"),
                "void": r.get("dark_face_void_near") or r.get("dark_face_void"),
                "stale": r.get("dark_face_stale"),
                "fog_rd": r.get("fog_pull_in_rd"),
                "kick": r.get("gpu_kick_n") or r.get("mesh_gpu_kick_n"),
                "mr": r.get("mark_relit_invoked_n") or r.get("markrelit_invoked_n"),
            }
        )
    out["tail_timeline"] = tl
    return out


def main() -> None:
    cur = summarize("manual_191112", ROOT / "bin/logs/perf_20260920-191112_44384.jsonl")
    sot = summarize("sot_143831", ROOT / "bin/logs/perf_20260920-143831_55228.jsonl")
    print(json.dumps({"current": cur, "sot": sot}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
