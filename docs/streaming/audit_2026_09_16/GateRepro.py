"""Audit-only negative controls for actual product eye-proxy implementation."""
import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
from flight_sim_run import compute_eye_proxy_stop_line

violations = 0
with tempfile.TemporaryDirectory(prefix="cubatarium-audit-gates-") as scratch:
    for label, extras in (
        ("persistent_100_holes", {"near_focus_holes": 100, "visual_holes": 100,
                                  "publication_incomplete_material_n": 0}),
        ("missing_holes_and_publication_fields", {}),
    ):
        path = Path(scratch) / (label + ".jsonl")
        rows = [{"kind": "period", "focus_cx": 3, "focus_cz": 3,
                 "movement_speed": 6, "mesh_apply_stale_visual": 0,
                 "transparent_cmd_reorder_n": 0, **extras} for _ in range(12)]
        path.write_text("\n".join(json.dumps(r) for r in rows), encoding="utf-8")
        result = compute_eye_proxy_stop_line(path)
        bad_pass = result.get("eye_proxy_stop_line_pass") is True
        violations += bad_pass
        print(f"{label}: incorrect_PASS={bad_pass} reasons={result.get('eye_proxy_stop_line_fails')}")
print(f"correctness_violations={violations}")
raise SystemExit(1 if violations else 0)
