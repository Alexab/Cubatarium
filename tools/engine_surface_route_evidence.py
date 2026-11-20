"""Observed ordinary walking; flight over a surface cannot satisfy this gate."""
import json
import math
from pathlib import Path


def evaluate(artifacts: Path, report: dict, *, minimum_chunks: int) -> dict:
    def result(status, reason, **values):
        return dict(schema="engine-surface-route-evidence.v1", status=status,
                    reason=reason, scope="flat saved surface, ordinary walking motor; not arbitrary terrain navigation", **values)

    trace = artifacts / "diagnostic_bot.jsonl"
    if not trace.is_file():
        return result("UNTESTED", "missing_surface_motor_trace")
    try:
        route = json.loads((artifacts / "bot-route.json").read_text(encoding="utf-8"))
        if route.get("movement_mode") != "walk":
            return result("FAIL", "surface_route_not_walking")
        rows = [json.loads(line) for line in trace.read_text(encoding="utf-8").splitlines() if line.strip()]
        if len(rows) < 5:
            return result("UNTESTED", "insufficient_surface_observations")
        previous_time = -1
        grounded = 0
        for row in rows:
            position = row["position"]
            time = row["time"]
            if (row.get("schema") != "diagnostic-bot-trace.v1" or
                    not isinstance(position, list) or len(position) != 3 or
                    any(type(v) not in (int, float) or not math.isfinite(v) for v in position) or
                    type(time) not in (int, float) or not math.isfinite(time) or time < previous_time):
                raise ValueError("invalid surface observation")
            previous_time = time
            for key in ("flying", "up", "down"):
                if type(row.get(key)) not in (int, bool) or row[key] != 0:
                    return result("FAIL", "non_walking_motor_input", field=key)
            if type(row.get("walking_ready")) not in (int, bool) or row["walking_ready"] not in (0, 1):
                raise ValueError("missing walking feedback")
            grounded += int(row["walking_ready"])
        bot = report.get("diagnostic_bot", {})
        if (report.get("teleport_cruise") is not False or
                type(report.get("diagnostic_relocations_after_start")) is not int or
                report["diagnostic_relocations_after_start"] != 0):
            return result("FAIL", "surface_relocation_or_missing_audit")
        if bot.get("state") != "complete" or rows[-1].get("state") != "complete":
            return result("FAIL", "surface_route_incomplete")
        origin = rows[0]["position"]
        excursion = max(max(abs(row["position"][k] - origin[k]) for k in (0, 2)) for row in rows)
        y_span = max(row["position"][1] for row in rows) - min(row["position"][1] for row in rows)
        if excursion < minimum_chunks * 16:
            return result("FAIL", "surface_excursion_too_short", excursion_blocks=excursion)
        if y_span > .2 or grounded / len(rows) < .9:
            return result("FAIL", "surface_support_not_retained", y_span=y_span, grounded_samples=grounded, samples=len(rows))
        return result("PASS", "ordinary_surface_walk_completed", excursion_blocks=excursion,
                      y_span=y_span, grounded_samples=grounded, samples=len(rows))
    except (OSError, ValueError, KeyError, TypeError, AttributeError):
        return result("FAIL", "malformed_surface_evidence")
