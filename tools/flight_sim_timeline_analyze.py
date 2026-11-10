#!/usr/bin/env python3
"""F0/F1 timeline analysis: compare phase snapshots and suggest fixes."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "bin"


def load(path: Path) -> dict:
    if not path.is_file():
        return {}
    return json.loads(path.read_text(encoding="utf-8"))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--timeline",
        type=Path,
        default=BIN / "iter_reports" / "timeline_summary.json",
    )
    ap.add_argument("--out", type=Path, default=BIN / "iter_reports" / "timeline_analysis.json")
    args = ap.parse_args()

    data = load(args.timeline)
    phases = data.get("phases") or []
    regressions = []
    for i in range(1, len(phases)):
        prev, cur = phases[i - 1], phases[i]
        for key in ("holes_rate", "wall_ms_med", "pending_light_focus_med", "spike_max_wall"):
            pv, cv = prev.get(key), cur.get(key)
            if pv is None or cv is None:
                continue
            if key == "holes_rate" and float(cv) > float(pv) + 0.05:
                regressions.append(f"{cur.get('phase')}: {key} regressed {pv}->{cv}")
            if key == "wall_ms_med" and float(cv) > float(pv) + 10:
                regressions.append(f"{cur.get('phase')}: {key} regressed {pv}->{cv}")
            if key == "pending_light_focus_med" and float(cv) > float(pv) + 4:
                regressions.append(f"{cur.get('phase')}: {key} regressed {pv}->{cv}")

    out = {
        "phase_count": len(phases),
        "regressions": regressions,
        "recommendations": regressions[:3] if regressions else ["no systemic regressions"],
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(out, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
