"""Explicit native edit/probe plans and independent persisted voxel evidence.

PASS is limited to material persistence, never light or final RGB correctness.
The binary reader intentionally does not call or import the native serializer.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import struct


def integer(value):
    return isinstance(value, int) and not isinstance(value, bool)


def validate_plan(plan, *, round_trip=False):
    if (not isinstance(plan, dict) or plan.get("schema") != "engine-native-world-actions.v1" or
            not isinstance(plan.get("entries"), list) or not 1 <= len(plan["entries"]) <= 8):
        raise ValueError("invalid bounded native world action plan")
    seen = set()
    for entry in plan["entries"]:
        if not isinstance(entry, dict):
            raise ValueError("invalid action entry")
        cell = entry.get("cell")
        if (not isinstance(cell, list) or len(cell) != 3 or
                any(not integer(v) or not -(1 << 31) <= v < (1 << 31) for v in cell)):
            raise ValueError("action cell must contain three int32 values")
        if tuple(cell) in seen:
            raise ValueError("duplicate action cell")
        seen.add(tuple(cell))
        for key in ("expected", "replacement") if "replacement" in entry else ("expected",):
            name = entry.get(key)
            if not isinstance(name, str) or not 1 <= len(name.encode("utf-8")) <= 128 or "\0" in name:
                raise ValueError("invalid material name")
        if "replacement" in entry and (not round_trip or entry["replacement"] == entry["expected"]):
            raise ValueError("mutation requires distinct material and return route")
    return plan


def read_binary_cell(data: bytes, section: tuple, local: tuple) -> int:
    """Strict x-fast CCHK v1..4 decoder, including whole-frame bounds checks."""
    offset = 0
    def take(fmt):
        nonlocal offset
        size = struct.calcsize("<" + fmt)
        if offset + size > len(data):
            raise ValueError("truncated binary chunk")
        value = struct.unpack_from("<" + fmt, data, offset)
        offset += size
        return value
    magic, version, cx, cy, cz, count = take("4sBiiiH")
    if magic != b"CCHK" or version not in (1, 2, 3, 4) or (cx, cy, cz) != section or not 1 <= count <= 4097:
        raise ValueError("binary chunk identity/version/palette")
    palette = take("H" * count)
    runs, = take("I")
    if not 1 <= runs <= 4096:
        raise ValueError("invalid run count")
    target = local[0] + 16*local[1] + 256*local[2]
    filled, actual = 0, None
    for _ in range(runs):
        length, index = take("HH")
        if length == 0 or index >= count or filled + length > 4096:
            raise ValueError("invalid palette run")
        if filled <= target < filled + length:
            actual = palette[index]
        filled += length
    if filled != 4096 or actual is None:
        raise ValueError("incomplete voxel payload")
    for minimum_version in (2, 3):
        if version >= minimum_version:
            size, = take("I")
            if size != 4096:
                raise ValueError("invalid fluid/light payload length")
            take("4096s")
    if version >= 4:
        take("HQ")
    if offset != len(data):
        raise ValueError("unexpected chunk trailing bytes")
    return actual


def read_disk_cell(world: Path, cell: list) -> dict:
    section = tuple(v // 16 for v in cell)
    local = tuple(v % 16 for v in cell)
    stem = world / "chunks" / "_".join(map(str, section))
    path = stem.with_suffix(".cchunk")
    if not path.exists():
        path = stem.with_suffix(".json")
    if path.is_symlink() or (world / "chunks").is_symlink():
        raise ValueError("foreign chunk path")
    data = path.read_bytes()
    if path.suffix == ".cchunk":
        actual = {"id": read_binary_cell(data, section, local)}
    else:
        document = json.loads(data.decode("utf-8"))
        version = document.get("format_version", 1)
        identity = tuple(document.get(k) for k in ("cx", "cy", "cz"))
        if (not integer(version) or version not in (1, 2, 3, 4) or
                any(not integer(v) for v in identity) or identity != section or
                not isinstance(document.get("voxels"), list) or len(document["voxels"]) > 4096):
            raise ValueError("JSON chunk identity/voxel count")
        seen, actual = set(), {"id": 0}
        for voxel in document["voxels"]:
            coord = tuple(voxel.get(k) for k in ("lx", "ly", "lz"))
            if any(not integer(v) or not 0 <= v < 16 for v in coord) or coord in seen:
                raise ValueError("JSON chunk duplicate/invalid local cell")
            seen.add(coord)
            if "id" in voxel:
                if not integer(voxel["id"]) or not 0 <= voxel["id"] <= 65535:
                    raise ValueError("invalid JSON material id")
                value = {"id": voxel["id"]}
            elif isinstance(voxel.get("type"), str) and voxel["type"]:
                value = {"type": voxel["type"]}
            else:
                raise ValueError("missing JSON material")
            if coord == local:
                actual = value
    return dict(path=str(path.resolve()), sha256=hashlib.sha256(data).hexdigest(),
                section=list(section), local=list(local), **actual)


def evaluate(artifacts: Path, world: Path, plan: dict, route_evidence: dict | None = None) -> dict:
    def result(status, reason, **evidence):
        return dict(schema="engine-world-actions-evidence.v1", status=status, reason=reason,
                    scope="observed material edit/probe and independent persisted voxel; no light/pixel/performance acceptance",
                    **evidence)
    path = artifacts / "world-actions.json"
    if not path.exists():
        return result("UNTESTED", "missing_native_world_actions")
    try:
        mutating = any("replacement" in e for e in plan["entries"])
        validate_plan(plan, round_trip=mutating)
        observation = json.loads(path.read_text(encoding="utf-8"))
        if (observation.get("schema") != "engine-world-actions-observation.v1" or
                observation.get("world") != world.name or observation.get("api") != "UWorld::ReplaceBlockAt" or
                observation.get("storage_matches_isolated_world") is not True or
                Path(observation["world_folder"]).resolve() != world.resolve() or
                observation.get("started") is not True or observation.get("failed") is not False):
            return result("FAIL", "native_action_failed_or_world_mismatch")
        rows = observation["observations"]
        if not isinstance(rows, list) or len(rows) != 2*len(plan["entries"]):
            raise ValueError("observation cardinality")
        if mutating and (not route_evidence or route_evidence.get("status") != "PASS"):
            return result("UNTESTED", "missing_verified_return_disk_cohort")
        evidence = []
        for entry in plan["entries"]:
            initial, final = ([row for row in rows if row.get("cell") == entry["cell"] and row.get("phase") == phase]
                              for phase in ("initial", "final"))
            if len(initial) != 1 or len(final) != 1:
                raise ValueError("ambiguous action observation")
            initial, final = initial[0], final[0]
            for row in (initial, final):
                if (row.get("resident") is not True or row.get("success") is not True or
                        any(not integer(row.get(k)) or not 0 <= row[k] <= 65535
                            for k in ("expected_id", "target_id", "before_id", "after_id")) or
                        not integer(row.get("incarnation")) or row["incarnation"] <= 0 or
                        not integer(row.get("steady_tick_ms")) or row["steady_tick_ms"] < 0):
                    raise ValueError("invalid material observation")
            expected, target = initial["expected_id"], initial["target_id"]
            if (initial["before_id"] != expected or initial["after_id"] != target or
                    final["before_id"] != target or final["after_id"] != target or
                    final["expected_id"] != expected or final["target_id"] != target or
                    initial["steady_tick_ms"] >= final["steady_tick_ms"] or
                    final.get("api_called") is not False or final.get("api_success") is not False):
                return result("FAIL", "material_transition_mismatch")
            section = [v // 16 for v in entry["cell"]]
            if initial.get("section") != section or final.get("section") != section:
                raise ValueError("section mismatch")
            if "replacement" in entry:
                reach = initial.get("reach_blocks")
                if (expected == target or initial.get("api_called") is not True or initial.get("api_success") is not True or
                        isinstance(reach, bool) or not isinstance(reach, (int,float)) or not 0 <= reach <= 6):
                    return result("FAIL", "missing_real_in_reach_edit")
                cohort = [s for s in route_evidence["sections"] if s["coord"] == section]
                if (len(cohort) != 1 or cohort[0]["retired_incarnation"] != initial["incarnation"] or
                        cohort[0]["returned_incarnation"] != final["incarnation"] or
                        initial["steady_tick_ms"] >= route_evidence["far_tick_ms"] or
                        final["steady_tick_ms"] < route_evidence["return_tick_ms"]):
                    return result("FAIL", "edit_not_joined_to_fresh_disk_cohort")
            elif (expected != target or initial.get("api_called") is not False or
                  initial.get("api_success") is not False):
                return result("FAIL", "read_only_probe_mutated")
            disk = read_disk_cell(world, entry["cell"])
            name = entry.get("replacement", entry["expected"])
            if (disk.get("id") != target if "id" in disk else disk.get("type") != name):
                return result("FAIL", "persisted_material_mismatch", disk=disk, cell=entry["cell"])
            evidence.append(dict(cell=entry["cell"], before_id=expected, after_id=target,
                                 initial_incarnation=initial["incarnation"], final_incarnation=final["incarnation"], disk=disk))
        return result("PASS", "edited_material_survived_fresh_disk_return" if mutating else "cold_material_probe_and_disk_agree", cells=evidence)
    except FileNotFoundError:
        return result("UNTESTED", "missing_persisted_chunk")
    except (ValueError, KeyError, TypeError, AttributeError, OSError, OverflowError, struct.error):
        return result("FAIL", "malformed_world_action_evidence")
