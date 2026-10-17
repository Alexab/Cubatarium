#!/usr/bin/env python3
import json
from pathlib import Path

REPORTS = [
    ("baseline", "bin/suite_reports/i18_hotfix_baseline_autofly.json"),
    ("fix1", "bin/suite_reports/i18_fix1_autofly.json"),
    ("fix234", "bin/suite_reports/i18_fix234_autofly.json"),
    ("manual", "bin/suite_reports/manual_20260901-102834_analyze.json"),
    ("gate", "bin/suite_reports/manual_20260831-214536_analyze.json"),
]
KEYS = [
    "wall_ms_fly_med", "stream_ms", "mesh_emerge_ms", "prep_refresh_gap_ms",
    "holes_rate", "effective_holes_blink_rate", "chain_stall_sec",
    "fm_dirty_to_gpu_finish_med", "cruise_capture_retarget_blocked_ratio",
    "cruise_capture_retarget_med", "cruise_schedule_ok_med", "schedule_ok_zero_rate",
    "ingress_debt_level", "visible_black_focus_n", "emerge_spike_frac",
]

for label, path in REPORTS:
    p = Path(path)
    if not p.is_file():
        print(f"{label}: MISSING {path}")
        continue
    d = json.loads(p.read_text(encoding="utf-8"))
    m = d.get("metrics", {})
    print(f"\n=== {label} ({p.name}) ===")
    for k in KEYS:
        v = m.get(k)
        if v is not None:
            print(f"  {k:42s} {v}")
