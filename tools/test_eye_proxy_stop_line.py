#!/usr/bin/env python3
"""N08 eye_proxy mid-corridor self-test on saved thrash flights."""
from __future__ import annotations

import json
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from flight_sim_run import compute_eye_proxy_stop_line  # noqa: E402

CASES = [
    (
        ROOT / "bin/logs/perf_20260915-095545_41740.jsonl",
        "095545",
        True,  # must FAIL (mid stale ~17)
    ),
    (
        ROOT / "bin/logs/perf_20260915-102527_35348.jsonl",
        "102527",
        False,  # stale mid≤6; hole-blink only (unfinished SoftDefer not blink)
    ),
    (
        ROOT / "bin/logs/perf_20260915-121131_5256.jsonl",
        "121131",
        True,
    ),
]


def _write_synthetic_incomplete(path: Path, incomplete_med: float) -> None:
    """Moving mid-corridor rows so incomplete gate can trip independently."""
    rows = []
    for i in range(9):
        rows.append(
            {
                "kind": "period",
                "focus_cx": 3.0,
                "movement_speed": 6.0,
                "mesh_apply_stale_visual": 1.0,
                "near_focus_holes": 0,
                "visual_holes": 0,
                "transparent_cmd_reorder_n": 0,
                "publication_incomplete_material_n": incomplete_med,
                "publication_oom_retain_n": 0,
            }
        )
    # West coverage metadata for scorecard (not required for incomplete gate).
    rows.append(
        {
            "kind": "period",
            "focus_cx": -3.0,
            "movement_speed": 6.0,
            "mesh_apply_stale_visual": 1.0,
            "near_focus_holes": 0,
            "visual_holes": 0,
            "transparent_cmd_reorder_n": 0,
            "publication_incomplete_material_n": incomplete_med,
            "publication_oom_retain_n": 0,
        }
    )
    path.write_text(
        "\n".join(json.dumps(r) for r in rows) + "\n", encoding="utf-8"
    )


def main() -> int:
    failures = 0
    for path, label, expect_fail in CASES:
        if not path.is_file():
            print(f"SKIP {label}: missing {path}")
            continue
        m = compute_eye_proxy_stop_line(path)
        passed = bool(m.get("eye_proxy_stop_line_pass"))
        fails = m.get("eye_proxy_stop_line_fails") or []
        stale = m.get("mesh_apply_stale_visual_mid_med")
        seg = m.get("eye_proxy_segment")
        print(
            f"{label}: pass={passed} segment={seg} stale_mid={stale} fails={fails}"
        )
        if expect_fail and passed:
            print(f"FAIL {label}: expected eye_proxy FAIL")
            failures += 1
        if expect_fail and "stale_visual_without_hole_counters" not in fails:
            # 095545/121131 have holes≈0 and stale mid>8
            if stale is not None and float(stale) > 8.0:
                print(
                    f"FAIL {label}: missing stale_visual_without_hole_counters "
                    f"(stale_mid={stale})"
                )
                failures += 1
        if not expect_fail and not passed:
            print(f"FAIL {label}: expected eye_proxy PASS")
            failures += 1
        if seg not in ("mid_corridor", "mid_third", "fly_fallback"):
            print(f"FAIL {label}: bad segment {seg}")
            failures += 1

    with tempfile.TemporaryDirectory() as tmp:
        high = Path(tmp) / "incomplete_high.jsonl"
        _write_synthetic_incomplete(high, 40.0)
        m = compute_eye_proxy_stop_line(high)
        fails = m.get("eye_proxy_stop_line_fails") or []
        print(
            f"synthetic_incomplete40: pass={m.get('eye_proxy_stop_line_pass')} "
            f"incomplete_mid={m.get('publication_incomplete_material_mid_med')} "
            f"fails={fails}"
        )
        if "incomplete_material_mid_med_above_eye_proxy" not in fails:
            print("FAIL synthetic: expected incomplete_material gate")
            failures += 1
        low = Path(tmp) / "incomplete_low.jsonl"
        _write_synthetic_incomplete(low, 1.0)
        m2 = compute_eye_proxy_stop_line(low)
        fails2 = m2.get("eye_proxy_stop_line_fails") or []
        print(
            f"synthetic_incomplete1: pass={m2.get('eye_proxy_stop_line_pass')} "
            f"fails={fails2}"
        )
        if "incomplete_material_mid_med_above_eye_proxy" in fails2:
            print("FAIL synthetic low: incomplete gate should not trip at 1")
            failures += 1

    if failures:
        print(f"{failures} failure(s)")
        return 1
    print("OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())