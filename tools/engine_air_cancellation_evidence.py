"""Actual partial AIR disk cancellation, gated by full primary/secondary return.

The probe request flag is never evidence. All identities come from native
queued requests, installed field owners, removals and discarded real results.
"""
from pathlib import Path

from engine_air_column_evidence import evaluate as evaluate_return, read_trace


def evaluate(artifacts: Path, report: dict, route: dict, *, column,
             min_chunk_y: int, max_chunk_y: int):
    def result(status, reason, **evidence):
        return dict(schema="engine-air-cancellation-evidence.v1", status=status, reason=reason,
                    scope="one controlled actual returning AIR disk demand cancellation/reissue; "
                          "no natural cancellation frequency, mixed geometry, general persistence, light, pixel or performance acceptance",
                    **evidence)
    returned = evaluate_return(artifacts, report, route, column=column,
                              min_chunk_y=min_chunk_y, max_chunk_y=max_chunk_y)
    if returned["status"] != "PASS":
        return result(returned["status"], "full_return_not_proven", returned=returned)
    try:
        events, complete = read_trace(artifacts / "secondary-column-residency.log")
        if not complete:
            return result("UNTESTED", "incomplete_cancellation_export")
        owner = next(e for e in events if e["source"] == "runtime_owner")
        runtime = [e for e in events if e["manager"] == owner["manager"]]
        probes = [e for e in runtime if e["source"] == "disk_air_probe_cancel"]
        if not probes:
            return result("UNTESTED", "no_actual_partial_cancellation")
        if len(probes) != 1:
            return result("FAIL", "ambiguous_probe_cancellation")
        cancel = probes[0]
        demand = cancel["value"]
        if not demand or demand == returned["disk_demand_id"]:
            return result("FAIL", "cancelled_and_returned_demand_identity_mismatch")
        queues = [e for e in runtime if e["source"] == "disk_column_queued" and e["value"] == demand]
        retired = [e for e in runtime if e["source"] == "disk_air_cancelled" and e["value"] == demand]
        if len(queues) != 1 or len(retired) != 1:
            return result("UNTESTED", "missing_cancelled_disk_demand_boundaries")
        queued, finished = queues[0], retired[0]
        if not (queued["seq"] < cancel["seq"] < finished["seq"] and
                returned["primary"]["far_tick_ms"] <= queued["tick"] <= cancel["tick"] <= finished["tick"]):
            return result("FAIL", "cancellation_not_on_returning_partial_demand")
        if any(e["source"] == "disk_column_complete" and e["value"] == demand for e in runtime):
            return result("FAIL", "cancelled_demand_claimed_complete")
        installs = [e for e in runtime if e["source"] == "disk_air_owned" and e["value"] == demand]
        profile_size = max_chunk_y - min_chunk_y + 1
        if not 0 < len(installs) < profile_size:
            return result("UNTESTED", "missing_partial_owned_air_installs")
        if len({e["coord"] for e in installs}) != len(installs):
            return result("FAIL", "duplicate_cancelled_air_ownership")
        by_seq = {e["seq"]: e for e in runtime}
        final = {tuple(e["coord"]): e["returned_field_incarnation"] for e in returned["sections"]}
        joined = []
        for installed in installs:
            field = by_seq.get(installed["seq"] - 1)
            if (not field or field["source"] != "air_install" or field["coord"] != installed["coord"] or
                    not field["value"] or not queued["seq"] < field["seq"] < installed["seq"] < cancel["seq"] or
                    installed["coord"] not in final):
                return result("FAIL", "cancelled_air_install_identity_mismatch")
            removal = [e for e in runtime if e["source"] == "air_remove" and
                       e["coord"] == field["coord"] and e["value"] == field["value"] and
                       cancel["seq"] < e["seq"] < finished["seq"]]
            if len(removal) != 1:
                return result("FAIL", "cancelled_air_owner_not_retired")
            if final[field["coord"]] == field["value"]:
                return result("FAIL", "fresh_return_reused_cancelled_field")
            joined.append(dict(coord=list(field["coord"]), cancelled_field_incarnation=field["value"],
                               returned_field_incarnation=final[field["coord"]]))
        fresh_queues = [e for e in runtime if e["source"] == "disk_column_queued" and
                        e["value"] == returned["disk_demand_id"] and e["seq"] > finished["seq"]]
        if len(fresh_queues) != 1:
            return result("UNTESTED", "missing_fresh_demand_after_cancellation")
        late = [e for e in runtime if e["source"] == "disk_result_cancelled" and
                e["value"] == demand and e["seq"] > finished["seq"]]
        if not late:
            return result("UNTESTED", "missing_actual_cancelled_result_discard")
        return result("PASS", "partial_air_demand_cancelled_retired_and_disk_reloaded",
                      cancelled_demand_id=demand, returned_demand_id=returned["disk_demand_id"],
                      cancelled_sections=joined, discarded_result_count=len(late), returned=returned)
    except (ValueError, KeyError, TypeError, AttributeError, OSError):
        return result("FAIL", "malformed_air_cancellation_evidence")
