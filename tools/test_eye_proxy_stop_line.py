#!/usr/bin/env python3
"""N08 eye_proxy mid-corridor self-test on saved thrash flights."""
from __future__ import annotations

import sys
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
        True,
    ),
    (
        ROOT / "bin/logs/perf_20260915-121131_5256.jsonl",
        "121131",
        True,
    ),
]


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
            # 095545/102527/121131 all have holes≈0 and stale mid>6
            if stale is not None and float(stale) > 6.0:
                print(
                    f"FAIL {label}: missing stale_visual_without_hole_counters "
                    f"(stale_mid={stale})"
                )
                failures += 1
        if seg not in ("mid_corridor", "mid_third", "fly_fallback"):
            print(f"FAIL {label}: bad segment {seg}")
            failures += 1
    if failures:
        print(f"{failures} failure(s)")
        return 1
    print("OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
