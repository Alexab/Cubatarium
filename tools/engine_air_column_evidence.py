"""Secondary decoded AIR data-column lifecycle, gated by the strict primary return.

Allocated fields and indexed absences cannot substitute for decoded terrain data.
This checks observed ownership and disk demand, not lighting, pixels, weather,
general persistence, performance, or memory bounds.
"""
from pathlib import Path
import re

from engine_round_trip_evidence import evaluate as evaluate_primary, integer


def read_trace(path):
    events = []
    summary = None
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("[ChunkResidencySummary] "):
            match = re.fullmatch(
                r"\[ChunkResidencySummary\] buffered=(\d+) reserved=(\d+) published=(\d+) "
                r"dropped=(\d+) unpublished=(\d+) complete=(\d+)", line)
            if summary is not None or not match:
                raise ValueError("summary")
            summary = tuple(map(int, match.groups()))
            continue
        if summary is not None:
            raise ValueError("event after summary")
        match = re.fullmatch(
            r"\[ChunkResidency\] seq=(\d+) tick_ms=(\d+) source=(\S+) "
            r"coord=\(([^)]+)\) origin=\(([^)]+)\) value=(\d+) manager=([\dA-Fa-f]+)", line)
        if not match:
            raise ValueError("event")
        seq, tick, source, coord, origin, value, manager = match.groups()
        coord, origin = tuple(map(int, coord.split(','))), tuple(map(int, origin.split(',')))
        if (int(seq) != len(events) + 1 or len(events) >= 4096 or
                len(coord) != 3 or len(origin) != 3 or
                any(not -(1 << 31) <= v < (1 << 31) for v in coord + origin) or
                int(value) >= (1 << 64) or len(manager) > 16):
            raise ValueError("event identity")
        events.append(dict(seq=int(seq), tick=int(tick), source=source, coord=coord,
                           origin=origin, value=int(value), manager=int(manager, 16)))
    complete = summary == (1, len(events), len(events), 0, 0, 1)
    return events, complete


def evaluate(artifacts: Path, report: dict, route: dict, *, column,
             min_chunk_y: int, max_chunk_y: int, minimum_chunks: int = 16):
    def result(status, reason, **evidence):
        return dict(schema="engine-air-column-evidence.v1", status=status, reason=reason,
                    scope="observed full-profile decoded AIR data retirement and fresh disk return; "
                          "no pixel/light/weather/general-persistence/performance/memory acceptance",
                    **evidence)

    if (not isinstance(column, (tuple, list)) or len(column) != 2 or
            any(not integer(v) or not -(1 << 31) <= v < (1 << 31) for v in column) or
            not integer(min_chunk_y) or not integer(max_chunk_y) or
            not -(1 << 31) <= min_chunk_y <= max_chunk_y < (1 << 31) or
            max_chunk_y - min_chunk_y >= 4096):
        return result("FAIL", "invalid_declared_air_profile")
    primary = evaluate_primary(artifacts, report, route, minimum_chunks=minimum_chunks, require_air=True)
    if primary["status"] != "PASS":
        return result(primary["status"], "primary_round_trip_not_proven", primary=primary)
    path = artifacts / "secondary-column-residency.log"
    if not path.is_file():
        return result("UNTESTED", "missing_secondary_air_trace")
    try:
        events, complete = read_trace(path)
        if not complete:
            return result("UNTESTED", "incomplete_secondary_air_export")
        primary_events, primary_complete = read_trace(artifacts / "round-trip-residency.log")
        primary_owners = [e for e in primary_events if e["source"] == "runtime_owner"]
        owners = [e for e in events if e["source"] == "runtime_owner"]
        if not primary_complete or len(primary_owners) != 1 or len(owners) != 1:
            return result("UNTESTED", "missing_unique_air_runtime_owner")
        owner = owners[0]
        if (owner["manager"] == 0 or owner["manager"] != primary_owners[0]["manager"] or
                [owner["coord"][0], owner["coord"][2]] != list(column) or
                [owner["origin"][0], owner["origin"][2]] != primary["column"] or
                list(column) == primary["column"] or
                any([e["coord"][0], e["coord"][2]] != list(column) for e in events)):
            return result("FAIL", "secondary_air_owner_identity_mismatch")
        runtime = [e for e in events if e["manager"] == owner["manager"]]
        if any(a["tick"] > b["tick"] for a, b in zip(runtime, runtime[1:])):
            raise ValueError("runtime event order")
        expected = {(column[0], cy, column[1]) for cy in range(min_chunk_y, max_chunk_y + 1)}
        cohorts = {}
        for phase in ("initial", "final"):
            for kind in ("air", "air_decoded"):
                rows = [e for e in runtime if e["source"] == f"runtime_{kind}_{phase}"]
                cohort = {e["coord"]: e for e in rows}
                if (set(cohort) != expected or len(rows) != len(expected) or
                        any(e["value"] == 0 for e in rows)):
                    return result("UNTESTED", "missing_full_profile_decoded_air_cohort", phase=phase, kind=kind)
                cohorts[kind, phase] = cohort
            if any(cohorts["air", phase][c]["value"] != cohorts["air_decoded", phase][c]["value"]
                   for c in expected):
                return result("FAIL", "decoded_air_field_identity_mismatch")
        initial, final = cohorts["air", "initial"], cohorts["air", "final"]
        begin = min(e["seq"] for e in initial.values())
        end = max(e["seq"] for e in cohorts["air_decoded", "final"].values())
        if any(e["source"] in ("runtime_initial", "runtime_final", "create", "air_materialize") and
               begin <= e["seq"] <= end for e in runtime):
            return result("FAIL", "air_data_column_materialized_geometry")
        retired = {}
        for coord, old in initial.items():
            removal = next((e for e in runtime if e["source"] in ("air_remove", "air_clear") and
                            e["coord"] == coord and e["value"] == old["value"] and
                            old["seq"] < e["seq"] and
                            old["tick"] <= e["tick"] <= primary["far_tick_ms"]), None)
            if removal is None:
                return result("FAIL", "air_data_column_not_retired", coord=list(coord))
            if final[coord]["value"] == old["value"]:
                return result("FAIL", "air_data_column_reused_retired_field", coord=list(coord))
            retired[coord] = removal
        queues = [e for e in runtime if e["source"] == "disk_column_queued" and
                  e["value"] > 0 and e["tick"] >= primary["far_tick_ms"]]
        for queued in queues:
            completions = [e for e in runtime if e["source"] == "disk_column_complete" and
                           e["value"] == queued["value"] and e["seq"] > queued["seq"] and
                           queued["tick"] <= e["tick"] <= primary["return_tick_ms"]]
            for completed in completions:
                joined = []
                for coord, old in initial.items():
                    returned = final[coord]
                    installs = [e for e in runtime if e["source"] == "air_install" and e["coord"] == coord and
                                e["value"] == returned["value"] and
                                retired[coord]["seq"] < queued["seq"] < e["seq"] < completed["seq"] and
                                queued["tick"] <= e["tick"] <= completed["tick"]]
                    decoded = cohorts["air_decoded", "final"][coord]
                    if (len(installs) != 1 or returned["seq"] <= completed["seq"] or
                            decoded["seq"] <= completed["seq"] or
                            min(returned["tick"], decoded["tick"]) < completed["tick"]):
                        break
                    joined.append(dict(coord=list(coord), retired_field_incarnation=old["value"],
                                       returned_field_incarnation=returned["value"]))
                if len(joined) == len(expected):
                    return result("PASS", "decoded_air_column_retired_and_disk_reloaded",
                                  column=list(column), min_chunk_y=min_chunk_y, max_chunk_y=max_chunk_y,
                                  sections=joined, disk_demand_id=queued["value"], primary=primary)
        return result("UNTESTED", "missing_fresh_air_column_disk_cohort")
    except (ValueError, TypeError, KeyError, AttributeError, OSError, OverflowError):
        return result("FAIL", "malformed_secondary_air_evidence")
