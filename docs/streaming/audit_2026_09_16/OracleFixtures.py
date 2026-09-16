#!/usr/bin/env python3
"""Minimal analytic coverage oracle fixtures (audit S3 MVP).

Fails closed when synthetic permanent holes or missing fields would have
passed the old eye-proxy. Not a pixel GPU oracle — gate honesty only.
"""
from __future__ import annotations

import json
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
from flight_sim_run import compute_eye_proxy_stop_line  # noqa: E402


def _rows(**extra):
    base = {
        "kind": "period",
        "focus_cx": 3.0,
        "movement_speed": 6.0,
        "mesh_apply_stale_visual": 0,
        "near_focus_holes": 0,
        "visual_holes": 0,
        "transparent_cmd_reorder_n": 0,
        "publication_incomplete_material_n": 0,
    }
    base.update(extra)
    return [dict(base) for _ in range(12)]


def main() -> int:
    violations = 0
    with tempfile.TemporaryDirectory() as tmp:
        cases = [
            ("solid_plane_ok", _rows(), True),
            ("permanent_holes", _rows(near_focus_holes=100, visual_holes=100), False),
            ("missing_fields", [{"kind": "period", "focus_cx": 3,
                                 "movement_speed": 6,
                                 "mesh_apply_stale_visual": 0}] * 12, False),
            ("last_block_delete_incomplete",
             _rows(publication_incomplete_material_n=40), False),
            ("checkerboard_holes",
             _rows(near_focus_holes=1, visual_holes=1,
                   mesh_apply_stale_visual=2), False),
            ("liquid_boundary_holes",
             _rows(near_focus_holes=2, visual_holes=2,
                   mesh_apply_stale_visual=0), False),
            ("temporal_fixed_camera_ok",
             _rows(movement_speed=6.0, focus_cx=3.0,
                   mesh_apply_stale_visual=1), True),
        ]
        for label, rows, expect_pass in cases:
            path = Path(tmp) / f"{label}.jsonl"
            path.write_text("\n".join(json.dumps(r) for r in rows), encoding="utf-8")
            result = compute_eye_proxy_stop_line(path)
            passed = bool(result.get("eye_proxy_stop_line_pass"))
            ok = passed == expect_pass
            print(f"{label}: pass={passed} expect_pass={expect_pass} "
                  f"fails={result.get('eye_proxy_stop_line_fails')}")
            if not ok:
                violations += 1
    print(f"correctness_violations={violations}")
    return 1 if violations else 0


if __name__ == "__main__":
    raise SystemExit(main())
