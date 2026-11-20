"""Native bounded decode faults, retained owners/local edit and rejected save.

Requires actual traces and byte snapshots; configuration flags cannot pass.
"""
import hashlib
import json
from pathlib import Path
import re

from engine_air_column_evidence import read_trace
from engine_round_trip_evidence import evaluate as evaluate_primary
from engine_world_actions import read_disk_cell


def evaluate(artifacts, report, route, *, column=(1, 5), worlds_root=None):
    artifacts = Path(artifacts)
    def result(status, reason, **evidence):
        return dict(schema="engine-disk-read-failure-evidence.v1", status=status, reason=reason,
                    scope="five controlled returning upper-frame decode failures, retained owners/local edit, "
                          "rejected metadata save and explicit recovery terrain save/cold persistence; no OS open/read fault or save-error UI acceptance",
                    **evidence)
    primary = evaluate_primary(artifacts, report, route, require_air=True)
    if primary["status"] != "PASS":
        return result(primary["status"], "primary_return_not_proven", primary=primary)
    try:
        events, complete = read_trace(artifacts / "secondary-column-residency.log")
        if not complete:
            return result("UNTESTED", "incomplete_read_failure_export")
        primary_events, _ = read_trace(artifacts / "round-trip-residency.log")
        primary_owner = next(e for e in primary_events if e["source"] == "runtime_owner")
        owners = [e for e in events if e["source"] == "runtime_owner"]
        if len(owners) != 1:
            return result("UNTESTED", "missing_unique_read_runtime_owner")
        owner = owners[0]
        if (owner["manager"] != primary_owner["manager"] or
                list(owner["origin"])[::2] != primary["column"] or
                any((e["coord"][0], e["coord"][2]) != tuple(column) for e in events)):
            return result("FAIL", "read_runtime_owner_mismatch")
        runtime = [e for e in events if e["manager"] == owner["manager"]]
        if any(a["tick"] > b["tick"] for a, b in zip(runtime, runtime[1:])):
            raise ValueError("event order")
        def rows(source): return [e for e in runtime if e["source"] == source]
        expected = {(column[0], cy, column[1]) for cy in range(-4, 20)}
        geometry = (column[0], -4, column[1])
        target = (column[0], 19, column[1])
        snapshots = {}
        for phase in ("initial", "final"):
            for kind, coords in (("", {geometry}), ("_decoded", {geometry}),
                                  ("_air", expected - {geometry}), ("_air_decoded", expected - {geometry})):
                entries = rows(f"runtime{kind}_{phase}")
                if len(entries) != len(coords) or {e["coord"] for e in entries} != coords or any(not e["value"] for e in entries):
                    return result("UNTESTED", "missing_full_decoded_read_cohort", phase=phase, kind=kind)
                snapshots[kind, phase] = {e["coord"]: e for e in entries}
            for kind, decoded in (("", "_decoded"), ("_air", "_air_decoded")):
                if any(e["value"] != snapshots[decoded, phase][coord]["value"]
                       for coord, e in snapshots[kind, phase].items()):
                    return result("FAIL", "decoded_owner_mismatch")
        initial = snapshots["", "initial"] | snapshots["_air", "initial"]
        final = snapshots["", "final"] | snapshots["_air", "final"]
        for coord, e in initial.items():
            removed = [r for r in runtime if r["source"] == ("remove" if coord == geometry else "air_remove") and
                       r["coord"] == coord and r["value"] == e["value"] and r["seq"] > e["seq"] and
                       e["tick"] <= r["tick"] <= primary["far_tick_ms"]]
            if not removed or final[coord]["value"] == e["value"]:
                return result("FAIL", "initial_read_cohort_not_retired")
        faults = rows("disk_read_frame_rejected")
        if len(faults) != 5 or any(e["coord"] != target for e in faults) or len({e["value"] for e in faults}) != 5:
            return result("UNTESTED", "missing_five_real_frame_rejections")
        queues = {e["value"]: e for e in rows("disk_column_queued") if e["tick"] >= primary["far_tick_ms"]}
        retries, exhausted = rows("disk_column_retry"), rows("disk_read_exhausted")
        bad_ids = [e["value"] for e in faults]
        if ([e["value"] for e in retries] != bad_ids[:4] or len(exhausted) != 1 or exhausted[0]["value"] != bad_ids[-1]):
            return result("UNTESTED", "missing_retry_exhaustion_chain")
        completes = rows("disk_column_complete")
        if len(completes) != 1 or completes[0]["value"] in bad_ids:
            return result("FAIL", "failed_demand_claimed_complete")
        completion = completes[0]
        recovered_id = completion["value"]
        if (recovered_id not in queues or queues[recovered_id]["seq"] <= exhausted[0]["seq"] or
                queues[recovered_id]["tick"] - exhausted[0]["tick"] < 999 or
                not any(e["value"] == recovered_id for e in rows("disk_read_recovered"))):
            return result("UNTESTED", "missing_bounded_recovery")
        recovery_saves = rows("disk_read_recovery_save_queued")
        if len(recovery_saves) != 1 or recovery_saves[0]["value"] != recovered_id or recovery_saves[0]["seq"] <= completion["seq"]:
            return result("UNTESTED", "missing_recovery_save_request")
        ids = bad_ids + [recovered_id]
        for index, demand in enumerate(ids):
            if demand not in queues:
                return result("UNTESTED", "missing_read_demand")
            start = queues[demand]["seq"]
            end = queues[ids[index+1]]["seq"] if index < 5 else completion["seq"]
            if index < 5 and not start < faults[index]["seq"] < end:
                return result("FAIL", "fault_demand_order_mismatch")
            accepted = [e for e in rows("disk_slice_read_valid") if e["value"] == demand]
            required = expected - ({target} if index < 5 else set())
            if {e["coord"] for e in accepted} != required or len(accepted) != len(required):
                return result("UNTESTED", "missing_valid_slices_during_retry")
            by_seq = {e["seq"]: e for e in runtime}
            for e in accepted:
                resident = by_seq.get(e["seq"]+1)
                if (not start < e["seq"] < end or not resident or resident["coord"] != e["coord"] or
                        resident["source"] != ("disk_geometry_owner" if e["coord"] == geometry else "disk_air_owner") or
                        resident["value"] != final[e["coord"]]["value"]):
                    return result("FAIL", "retained_read_owner_changed")
        for coord, e in final.items():
            if any(r["coord"] == coord and r["value"] == e["value"] and
                   r["source"] in ("remove", "air_remove", "air_clear") and r["seq"] > queues[bad_ids[0]]["seq"]
                   for r in runtime):
                return result("FAIL", "retained_read_data_discarded")
        edit_before, edit_after, edit_owner = rows("disk_read_edit_before"), rows("disk_read_edit_after"), rows("disk_read_edit_owner")
        if len(edit_before) != 1 or len(edit_after) != 1 or len(edit_owner) != 1:
            return result("UNTESTED", "missing_controlled_local_edit")
        before, after, edited = edit_before[0], edit_after[0], edit_owner[0]
        cell = (column[0]*16+1, -63, column[1]*16+1)
        current = [e for e in rows("disk_read_edit_current") if e["seq"] > edited["seq"]]
        if (before["coord"] != geometry or tuple(before["origin"]) != cell or before["value"] == 0 or
                after["coord"] != geometry or after["value"] != 0 or
                not before["seq"] < after["seq"] < edited["seq"] or
                edited["value"] != final[geometry]["value"] or len(current) < 5 or
                any(e["value"] != 0 for e in current)):
            return result("FAIL", "local_edit_overwritten_during_retry")
        rejected = rows("disk_read_save_rejected")
        dirty_before, dirty_after = rows("disk_read_dirty_before"), rows("disk_read_dirty_after")
        if (len(rejected) != 1 or rejected[0]["value"] != bad_ids[0] or rows("disk_read_save_accepted") or
                rows("disk_read_save_unexpected_exception") or len(dirty_before) != 1 or len(dirty_after) != 1 or
                not dirty_before[0]["value"] or dirty_before[0]["value"] != dirty_after[0]["value"]):
            return result("FAIL", "blocked_save_not_rejected_with_dirty_state")
        snapshot = artifacts/"disk-read-save"
        manifest = json.loads((snapshot/"manifest.json").read_text(encoding="utf-8"))
        files = manifest.get("files", [])
        if (manifest.get("schema") != "disk-read-save-files.v1" or manifest.get("demand_id") != bad_ids[0] or
                manifest.get("column") != list(column) or not 24 <= len(files) <= 64 or
                len({e["path"] for e in files}) != len(files)):
            raise ValueError("manifest")
        hashes = {}
        if sum(e["bytes"] for e in files) > 16*1024*1024 or any(type(e["bytes"]) is not int or e["bytes"] < 0 for e in files):
            raise ValueError("snapshot bound")
        for e in files:
            relative = Path(e["path"])
            if relative.is_absolute() or '..' in relative.parts:
                raise ValueError("snapshot path")
            data = []
            for phase in ("before", "after"):
                p = snapshot/phase/relative
                if p.is_symlink() or not p.resolve().is_relative_to((snapshot/phase).resolve()):
                    raise ValueError("foreign snapshot")
                data.append(p.read_bytes())
            if len(data[0]) != e["bytes"] or data[0] != data[1]:
                return result("FAIL", "blocked_save_modified_files")
            hashes[e["path"]] = hashlib.sha256(data[0]).hexdigest()
        if (read_disk_cell(snapshot/"before", list(cell)).get("id") != before["value"] or
                not any(e["value"] == len(files) for e in rows("disk_read_files_snapshot"))):
            return result("FAIL", "blocked_save_snapshot_identity_mismatch")
        if not {"world_data.json", "users.json"}.issubset(hashes):
            raise ValueError("metadata snapshot")
        for coord in expected:
            sample = [coord[0]*16+1, coord[1]*16+1, coord[2]*16+1]
            observed = read_disk_cell(snapshot/"before", sample)
            if observed.get("id") != (before["value"] if coord == geometry else 0):
                return result("FAIL", "saved_fixture_payload_mismatch")
        runtime_route = json.loads((artifacts/"route.json").read_text(encoding="utf-8"))
        name = runtime_route["fixture_world"]
        if not re.fullmatch(r"EngineSmoke_[0-9a-f]{32}", name) or name == runtime_route["world_source"]:
            raise ValueError("world isolation")
        root = Path(worlds_root) if worlds_root else Path(__file__).resolve().parents[1]/"bin/worlds"
        cold = read_disk_cell(root/name, list(cell))
        if cold.get("id") != 0:
            return result("FAIL", "local_edit_missing_from_final_disk", cold=cold)
        return result("PASS", "failed_reads_preserved_data_edit_and_save_guard_then_recovered",
                      failed_demands=bad_ids, recovered_demand_id=recovered_id,
                      retained_slices=23, edited_geometry_incarnation=edited["value"],
                      dirty_chunks_preserved=dirty_before[0]["value"], files_preserved=hashes,
                      cold_local_edit=cold, primary=primary)
    except (OSError, ValueError, KeyError, TypeError, AttributeError, OverflowError):
        return result("UNTESTED", "missing_or_malformed_read_failure_evidence")
