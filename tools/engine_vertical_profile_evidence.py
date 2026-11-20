"""Native profile identity and saved-position retention in a supported nondefault profile."""
import json
import math
from pathlib import Path


def evaluate(artifacts: Path, report: dict, scene: dict, witness: dict | None):
    def result(status, reason, **values):
        return dict(schema="engine-vertical-profile-evidence.v1", status=status, reason=reason,
                    scope="configured native vertical range and safe negative saved-player pose retained; no intentional invalid-pose recovery or GPU parity", **values)
    try:
        profile = scene["custom_vertical_profile"]
        lo, hi = profile["min_y"], profile["max_y"]
        if type(lo) is not int or type(hi) is not int or not -64 <= lo <= 0 or not lo < hi <= 319 or (lo, hi) == (-64, 319):
            raise ValueError("invalid custom profile")
        if witness is None:
            return result("UNTESTED", "missing_native_profile_witness")
        if witness.get("min_y") != lo or witness.get("max_y") != hi:
            return result("FAIL", "native_profile_range_mismatch")
        if (report.get("teleport_cruise") is not False or
                type(report.get("diagnostic_relocations_after_start")) is not int or
                report["diagnostic_relocations_after_start"] != 0):
            return result("FAIL", "profile_route_relocated_or_missing_audit")
        rows = [json.loads(line) for line in (artifacts/"diagnostic_bot.jsonl").read_text(encoding="utf-8").splitlines()]
        if len(rows) < 3:
            return result("UNTESTED", "insufficient_custom_profile_poses")
        saved = scene["starting_eye"]
        if saved[1] >= 0 or math.dist(saved, rows[0]["position"]) > .25:
            return result("FAIL", "saved_custom_pose_not_retained")
        for row in rows:
            position = row["position"]
            if (not isinstance(position, list) or len(position) != 3 or
                    any(type(v) not in (int, float) or not math.isfinite(v) for v in position) or
                    not lo-.5 <= position[1] < 0):
                return result("FAIL", "route_not_within_custom_custom_band")
        if report.get("diagnostic_bot", {}).get("state") != "complete":
            return result("FAIL", "custom_profile_route_incomplete")
        return result("PASS", "custom_saved_pose_and_configured_profile_retained",
                      min_y=lo, max_y=hi, saved_eye=saved, samples=len(rows),
                      observed_eye_y=[min(r["position"][1] for r in rows), max(r["position"][1] for r in rows)])
    except (OSError, ValueError, TypeError, KeyError, AttributeError) as error:
        return result("FAIL", "invalid_vertical_profile_evidence", diagnostic=str(error))
