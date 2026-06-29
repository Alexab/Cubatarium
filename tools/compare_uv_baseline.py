#!/usr/bin/env python3
"""Compare uv_quality_report.yaml against frozen baseline for regressions."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Any

import yaml

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))

from creature_tier_a import TIER_A_MOBS
from creature_uv_common import load_thresholds


def load_report(path: Path) -> dict:
    if not path.is_file():
        return {}
    return yaml.safe_load(path.read_text(encoding="utf-8")) or {}


def metric_value(entry: dict, key: str) -> float | None:
    metrics = entry.get("metrics") or {}
    if key in metrics and isinstance(metrics[key], (int, float)):
        return float(metrics[key])
    if key == "face_bleed_score":
        return metrics.get("face_bleed_score")
    if key == "snout_front_score":
        return metrics.get("snout_front_score")
    if key == "b3d_assignment_rate":
        return metrics.get("b3d_assignment_rate")
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, default=TOOLS / "uv_quality_baseline.yaml")
    parser.add_argument("--current", type=Path, default=TOOLS / "uv_quality_report.yaml")
    parser.add_argument("--species", nargs="+")
    parser.add_argument("--tier-a", action="store_true")
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--fail-on-regression", action="store_true")
    args = parser.parse_args()

    baseline = load_report(args.baseline)
    current = load_report(args.current)
    if not baseline:
        print(f"no baseline at {args.baseline} (skip regression)")
        return 0
    if not current:
        print(f"no current report at {args.current}")
        return 1

    if args.all:
        species = sorted(set(baseline) & set(current))
    elif args.tier_a:
        species = [s for s in TIER_A_MOBS if s in baseline and s in current]
    elif args.species:
        species = args.species
    else:
        species = [s for s in TIER_A_MOBS if s in baseline and s in current]

    eps = (load_thresholds().get("regression_epsilon") or {})
    ratio_eps = float(eps.get("ratio", 0.05))
    de_eps = float(eps.get("delta_e", 2.0))

    regressions = 0
    keys_higher_better = ("face_bleed_score", "snout_front_score", "b3d_assignment_rate")
    for sid in species:
        if sid == "summary":
            continue
        b = baseline.get(sid) or {}
        c = current.get(sid) or {}
        for key in keys_higher_better:
            bv = metric_value(b, key)
            cv = metric_value(c, key)
            if bv is None or cv is None:
                continue
            if key in ("face_bleed_score",):
                if cv < bv - de_eps:
                    print(f"REGRESSION {sid} {key}: {cv:.2f} < baseline {bv:.2f}")
                    regressions += 1
            elif cv < bv - ratio_eps:
                print(f"REGRESSION {sid} {key}: {cv:.3f} < baseline {bv:.3f}")
                regressions += 1
        if c.get("pass") is False and b.get("pass") is True:
            print(f"REGRESSION {sid}: was pass, now fail")
            regressions += 1

    if regressions:
        print(f"regressions: {regressions}")
        return 1 if args.fail_on_regression else 0
    print(f"OK: no regression in {len(species)} species")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
