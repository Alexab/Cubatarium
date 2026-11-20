"""Route geometry evidence independent of screenshots and rendering proxies."""
import json
import math
from pathlib import Path


def vector(value):
    return (isinstance(value, list) and len(value) == 3 and
            all(type(v) in (int, float) and math.isfinite(v) for v in value))


def evaluate(trace: Path, report: dict, route: dict, *, minimum_chunks: int,
             scene: dict | None = None) -> dict:
    def result(status, reason, **values):
        return dict(schema="engine-route-path-evidence.v1", status=status, reason=reason,
                    scope="ordered observed waypoints and cave containment; no current-EXE, visual, light or performance acceptance", **values)

    if not trace.is_file():
        return result("UNTESTED", "missing_route_path_trace")
    try:
        points = route["waypoints"]
        if (route.get("schema") != "diagnostic-flight-route.v1" or
                type(route.get("relative")) is not bool or not isinstance(points, list) or
                not 1 <= len(points) <= 256 or any(not vector(p["position"]) for p in points)):
            raise ValueError("invalid route")
        rows = [json.loads(line) for line in trace.read_text(encoding="utf-8").splitlines() if line.strip()]
        if len(rows) < 3:
            return result("UNTESTED", "insufficient_route_path_observations")
        previous = -1
        for row in rows:
            time = row["time"]
            if (row.get("schema") != "diagnostic-bot-trace.v1" or not vector(row["position"]) or
                    type(time) not in (int, float) or not math.isfinite(time) or time < previous):
                raise ValueError("invalid pose/time")
            previous = time
        bot = report["diagnostic_bot"]
        if (report.get("teleport_cruise") is not False or
                type(report.get("diagnostic_relocations_after_start")) is not int or
                report["diagnostic_relocations_after_start"] != 0):
            return result("FAIL", "route_relocation_or_missing_audit")
        if (bot.get("enabled") is not True or bot.get("state") != "complete" or
                type(bot.get("waypoints_visited")) is not int or bot["waypoints_visited"] != len(points) or
                rows[-1].get("state") != "complete" or rows[-1].get("waypoints_visited") != len(points)):
            return result("FAIL", "route_not_completed")
        origin = rows[0]["position"] if route["relative"] else [0, 0, 0]
        matched = []
        cursor = 0
        for index, point in enumerate(points):
            target = [a+b for a, b in zip(origin, point["position"])]
            found = None
            for at in range(cursor, len(rows)):
                row = rows[at]
                if (row.get("state") == "inspect" and row.get("waypoints_visited") == index and
                        math.dist(row["position"], target) <= .7):
                    found = at
                    break
            if found is None:
                return result("FAIL", "ordered_waypoint_not_observed", waypoint=index, target=target)
            matched.append(dict(waypoint=index, time=rows[found]["time"], position=rows[found]["position"]))
            cursor = found + 1
        excursion = max(max(abs(row["position"][k]-rows[0]["position"][k]) for k in (0, 2)) for row in rows)
        if excursion < minimum_chunks * 16:
            return result("FAIL", "route_excursion_too_short", excursion_blocks=excursion)
        if scene is not None:
            bounds = scene["cave_bounds"]
            lo, hi = scene["chunk_x_bounds"]
            if not vector(scene["starting_eye"]) or math.dist(rows[0]["position"], scene["starting_eye"]) > 2:
                return result("FAIL", "cave_start_mismatch")
            if type(scene["expected_eye_in_fluid"]) is not bool:
                raise ValueError("invalid fluid expectation")
            for row in rows:
                x, y, z = row["position"]
                if not (lo*16-.5 <= x <= (hi+1)*16-.5 and
                        bounds["floor"] < y < bounds["ceiling"] and bounds["z_low"] < z < bounds["z_high"]):
                    return result("FAIL", "route_left_authored_cave", position=row["position"])
                if row.get("eye_in_fluid") is not scene["expected_eye_in_fluid"]:
                    return result("FAIL", "cave_fluid_observation_mismatch")
        return result("PASS", "ordered_route_path_observed", waypoints=matched,
                      samples=len(rows), excursion_blocks=excursion, cave_containment=scene is not None)
    except (OSError, ValueError, TypeError, KeyError, AttributeError):
        return result("FAIL", "malformed_route_path_evidence")
