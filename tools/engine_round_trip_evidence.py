"""Motor out-and-back evidence joined to actual initial-cohort disk reload.

This certifies the observed data lifecycle only, never pixels, physical light,
performance or general persistence. Missing or truncated evidence cannot pass.
"""
from __future__ import annotations
import json
import math
from pathlib import Path
import re


def integer(value):
    return isinstance(value, int) and not isinstance(value, bool)


def vector(value):
    return (isinstance(value, list) and len(value) == 3 and
            all(isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v) for v in value))


def evaluate(artifacts: Path, report: dict, route: dict, *, minimum_chunks: int = 16, require_air: bool = False) -> dict:
    scope = "observed motor excursion, return and initial resident-section retirement/fresh disk reload; no pixel/light/performance acceptance"
    if require_air:
        scope += "; includes explicit loaded AIR field retirement and fresh disk reload"
    def result(status, reason, **evidence):
        return dict(schema="engine-round-trip-evidence.v1", status=status, reason=reason, scope=scope, **evidence)
    if not isinstance(report, dict) or not isinstance(route, dict):
        return result("FAIL", "malformed_round_trip_evidence")
    if report.get("teleport_cruise") is True or report.get("diagnostic_relocations_after_start", 0) != 0:
        return result("FAIL", "relocation_during_round_trip")
    if (report.get("teleport_cruise") is not False or
            not integer(report.get("diagnostic_relocations_after_start")) or
            not integer(report.get("initial_resident_column_count")) or report["initial_resident_column_count"] <= 0 or
            not integer(report.get("outside_initial_resident_samples")) or report["outside_initial_resident_samples"] <= 0):
        return result("UNTESTED", "missing_round_trip_native_audit")
    bot = report.get("diagnostic_bot", {})
    if bot.get("enabled") is not True or bot.get("state") != "complete" or not integer(bot.get("waypoints_visited")) or bot.get("waypoints_visited") != 3:
        return result("FAIL", "round_trip_bot_incomplete")
    trace = artifacts / "round-trip-residency.log"
    poses = artifacts / "diagnostic_bot.jsonl"
    if not trace.is_file() or not poses.is_file():
        return result("UNTESTED", "missing_round_trip_trace")
    try:
        points = route["waypoints"]
        if (route.get("purpose") != "round_trip" or route.get("relative") is not True or
                len(points) != 3 or any(not vector(p.get("position")) for p in points) or
                points[0]["position"] != points[2]["position"]):
            raise ValueError("route shape")
        observations = [json.loads(line) for line in poses.read_text(encoding="utf-8").splitlines() if line.strip()]
        if len(observations) < 3:
            return result("UNTESTED", "insufficient_round_trip_observations")
        previous_tick = previous_time = -1
        for row in observations:
            tick, time = row.get("steady_tick_ms"), row.get("time")
            if (row.get("schema") != "diagnostic-bot-trace.v1" or not vector(row.get("position")) or
                    not integer(tick) or tick < 0 or tick < previous_tick or not isinstance(time, (int, float)) or
                    isinstance(time, bool) or not math.isfinite(time) or time < 0 or time < previous_time):
                raise ValueError("pose")
            previous_tick, previous_time = tick, time
        origin = observations[0]["position"]
        far_target = [a+b for a,b in zip(origin, points[1]["position"])]
        return_target = [a+b for a,b in zip(origin, points[2]["position"])]
        excursion = max(max(abs(row["position"][k] - origin[k]) for k in (0,2)) for row in observations)
        far = next((row for row in observations if math.dist(row["position"], far_target) <= 1.5), None)
        if far is None or excursion < minimum_chunks * 16:
            return result("FAIL", "round_trip_frontier_not_reached", maximum_excursion_blocks=excursion)
        returns = [row for row in observations if row["steady_tick_ms"] > far["steady_tick_ms"] and
                   math.dist(row["position"], return_target) <= 1.5]
        if not returns or math.dist(observations[-1]["position"], return_target) > 1.5:
            return result("FAIL", "round_trip_origin_not_regained")
        events, summary = [], None
        for line in trace.read_text(encoding="utf-8").splitlines():
            if line.startswith("[ChunkResidencySummary] "):
                if summary is not None: raise ValueError("duplicate summary")
                summary = dict(re.findall(r"(\w+)=(\d+)", line))
                continue
            if summary is not None: raise ValueError("event after summary")
            match = re.fullmatch(r"\[ChunkResidency\] seq=(\d+) tick_ms=(\d+) source=(\S+) coord=\(([^)]+)\) origin=\(([^)]+)\) value=(\d+) manager=([\dA-Fa-f]+)", line)
            if not match: raise ValueError("event")
            seq,tick,source,coord,owner,value,manager = match.groups()
            coord,owner = [int(v) for v in coord.split(',')], [int(v) for v in owner.split(',')]
            if (len(coord) != 3 or len(owner) != 3 or int(seq) != len(events)+1 or
                    any(v < -(1 << 31) or v >= (1 << 31) for v in coord+owner) or
                    int(value) >= (1 << 64) or len(manager) > 16 or len(events) >= 4096):
                raise ValueError("event identity")
            events.append(dict(seq=int(seq), tick=int(tick), source=source, coord=tuple(coord), origin=owner,
                               value=int(value), manager=manager))
        if (summary is None or summary.get("buffered") != "1" or summary.get("complete") != "1" or
                summary.get("dropped") != "0" or summary.get("unpublished") != "0" or
                int(summary.get("reserved", -1)) != len(events) or int(summary.get("published", -1)) != len(events)):
            return result("UNTESTED", "incomplete_round_trip_residency_export")
        owners = [e for e in events if e["source"] == "runtime_owner"]
        if len(owners) != 1 or int(owners[0]["manager"],16) == 0:
            return result("UNTESTED", "missing_unique_runtime_owner")
        owner = owners[0]
        column = [owner["coord"][0], owner["coord"][2]]
        if (report.get("start_focus") != column or report.get("end_focus") != column or
                [owner["origin"][0], owner["origin"][2]] != column):
            return result("FAIL", "round_trip_column_identity_mismatch")
        runtime = [e for e in events if e["manager"] == owner["manager"]]
        if any([e["coord"][0],e["coord"][2]] != column for e in runtime): raise ValueError("selected column")
        if any(a["tick"] > b["tick"] for a,b in zip(runtime,runtime[1:])): raise ValueError("runtime event order")
        initial_rows = [e for e in runtime if e["source"] == "runtime_initial"]
        final_rows = [e for e in runtime if e["source"] == "runtime_final"]
        initial = {e["coord"]:e for e in initial_rows}
        final = {e["coord"]:e for e in final_rows}
        if (not initial or len(initial) != len(initial_rows) or len(final) != len(final_rows) or
                any(e["value"] == 0 for e in initial_rows+final_rows)):
            return result("UNTESTED", "missing_initial_resident_cohort")
        retired = {}
        for coord,entry in initial.items():
            removal = next((e for e in runtime if e["source"] == "remove" and e["coord"] == coord and
                            e["value"] == entry["value"] and entry["tick"] <= e["tick"] <= far["steady_tick_ms"]), None)
            if removal is None: return result("FAIL", "initial_cohort_not_evicted", coord=list(coord))
            retired[coord] = removal
        air_initial = {}
        air_final = {}
        if require_air:
            starts = [e for e in runtime if e["source"] == "runtime_air_initial"]
            ends = [e for e in runtime if e["source"] == "runtime_air_final"]
            air_initial = {e["coord"]: e for e in starts}
            air_final = {e["coord"]: e for e in ends}
            if (not starts or not ends or len(starts) != len(air_initial) or
                    len(ends) != len(air_final) or any(e["value"] == 0 for e in starts+ends)):
                return result("UNTESTED", "missing_initial_or_final_air_cohort")
            if set(air_initial) & set(initial) or set(air_final) & set(final):
                return result("FAIL", "air_cohort_has_geometry")
            for coord, entry in air_initial.items():
                removal = next((e for e in runtime if e["source"] in ("air_remove", "air_clear") and
                                e["coord"] == coord and e["value"] == entry["value"] and
                                entry["seq"] < e["seq"] and
                                entry["tick"] <= e["tick"] <= far["steady_tick_ms"]), None)
                if removal is None:
                    return result("FAIL", "initial_air_cohort_not_evicted", coord=list(coord))
                if any(e["source"] in ("create", "air_materialize") and e["coord"] == coord and
                       entry["tick"] <= e["tick"] <= max(x["tick"] for x in ends) for e in runtime):
                    return result("FAIL", "air_cohort_materialized_geometry", coord=list(coord))
                if coord in air_final and air_final[coord]["value"] == entry["value"]:
                    return result("FAIL", "returned_air_reuses_retired_owner", coord=list(coord))
        queues = [e for e in runtime if e["source"] == "disk_column_queued" and e["value"] > 0 and
                  e["tick"] >= far["steady_tick_ms"]]
        for queued in queues:
            completed = next((e for e in runtime if e["source"] == "disk_column_complete" and
                              e["value"] == queued["value"] and queued["tick"] <= e["tick"] <= observations[-1]["steady_tick_ms"]), None)
            if completed is None: continue
            joined = []
            for coord,entry in initial.items():
                returned = final.get(coord)
                created = next((e for e in runtime if e["source"] == "create" and e["coord"] == coord and
                                e["value"] != entry["value"] and queued["tick"] <= e["tick"] <= completed["tick"]), None)
                if (created is None or returned is None or returned["value"] != created["value"] or
                        returned["tick"] < completed["tick"] or retired[coord]["tick"] >= created["tick"]): break
                joined.append(dict(coord=list(coord), retired_incarnation=entry["value"], returned_incarnation=created["value"]))
            if len(joined) == len(initial):
                air_joined = []
                for coord, entry in air_initial.items():
                    returned = air_final.get(coord)
                    installed = next((e for e in runtime if e["source"] == "air_install" and
                                      e["coord"] == coord and e["value"] != entry["value"] and
                                      queued["seq"] < e["seq"] < completed["seq"] and
                                      queued["tick"] <= e["tick"] <= completed["tick"]), None)
                    if (installed is None or returned is None or returned["value"] != installed["value"] or
                            returned["seq"] <= completed["seq"] or returned["tick"] < completed["tick"]):
                        break
                    air_joined.append(dict(coord=list(coord), retired_field_incarnation=entry["value"],
                                           returned_field_incarnation=installed["value"]))
                if require_air and len(air_joined) != len(air_initial):
                    continue
                extra = dict(air_sections=air_joined) if require_air else {}
                return result("PASS", "motor_return_with_fresh_disk_cohort", column=column, sections=joined,
                              **extra,
                              maximum_excursion_blocks=excursion, disk_demand_id=queued["value"],
                              far_tick_ms=far["steady_tick_ms"], return_tick_ms=returns[0]["steady_tick_ms"])
        return result("UNTESTED", "missing_fresh_return_disk_cohort")
    except (ValueError, KeyError, TypeError, AttributeError, OSError, OverflowError):
        return result("FAIL", "malformed_round_trip_evidence")
