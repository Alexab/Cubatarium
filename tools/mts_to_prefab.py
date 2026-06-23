#!/usr/bin/env python3
"""Convert Minetest .mts schematics to Cubatarium prefab JSON."""

from __future__ import annotations

import argparse
import json
import struct
import sys
import zlib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import yaml

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))

from prefab_import_common import (  # noqa: E402
    load_known_block_names,
    load_manifest,
    load_node_map,
    manifest_entry,
    resolve_block,
)

MTSM_SIGNATURE = 0x4D54534D
PROB_NEVER = 0x00
PROB_ALWAYS = 0x7F
PROB_ALWAYS_OLD = 0xFF
FORCE_PLACE = 0x80


@dataclass
class MtsNode:
    x: int
    y: int
    z: int
    name: str
    param1: int
    force: bool


@dataclass
class ImportReport:
    source: str
    prefab_name: str
    total_solid: int = 0
    mapped: int = 0
    skipped_nodes: list[str] = field(default_factory=list)
    unmapped_nodes: list[str] = field(default_factory=list)
    block_count: int = 0

    @property
    def unmapped_ratio(self) -> float:
        if self.total_solid == 0:
            return 0.0
        return len(set(self.unmapped_nodes)) / max(1, self.total_solid)


def read_mts(path: Path) -> tuple[tuple[int, int, int], list[str], list[MtsNode]]:
    data = path.read_bytes()
    offset = 0

    def u16() -> int:
        nonlocal offset
        val = struct.unpack_from(">H", data, offset)[0]
        offset += 2
        return val

    def u32() -> int:
        nonlocal offset
        val = struct.unpack_from(">I", data, offset)[0]
        offset += 4
        return val

    sig = u32()
    if sig != MTSM_SIGNATURE:
        raise ValueError(f"{path}: bad signature {sig:#x}")

    version = u16()
    if version > 4:
        raise ValueError(f"{path}: unsupported version {version}")

    size_x, size_y, size_z = u16(), u16(), u16()
    offset += size_y  # slice probabilities

    name_count = u16()
    names: list[str] = []
    for _ in range(name_count):
        name_len = u16()
        name = data[offset : offset + name_len].decode("utf-8", errors="replace")
        offset += name_len
        names.append(name)

    compressed = data[offset:]
    raw = zlib.decompress(compressed)
    volume = size_x * size_y * size_z
    if len(raw) < volume * 4:
        raise ValueError(f"{path}: truncated schematic data")

    content_ids = struct.unpack(f">{volume}H", raw[: volume * 2])
    param1 = raw[volume * 2 : volume * 3]
    # param2 = raw[volume * 3 : volume * 4]

    nodes: list[MtsNode] = []
    idx = 0
    for z in range(size_z):
        for y in range(size_y):
            for x in range(size_x):
                cid = content_ids[idx]
                p1 = param1[idx]
                idx += 1
                if cid >= len(names):
                    continue
                name = names[cid]
                force = bool(p1 & FORCE_PLACE)
                prob = p1 & 0x7F
                if prob == PROB_NEVER and not force:
                    continue
                nodes.append(MtsNode(x, y, z, name, p1, force))
    return (size_x, size_y, size_z), names, nodes


def compute_anchor(nodes: list[tuple[int, int, int, bool]]) -> tuple[int, int, int]:
    if not nodes:
        return (0, 0, 0)
    forced = [n for n in nodes if n[3]]
    pool = forced if forced else nodes
    min_y = min(n[1] for n in pool)
    xs = [n[0] for n in pool if n[1] == min_y]
    zs = [n[2] for n in pool if n[1] == min_y]
    ax = (min(xs) + max(xs)) // 2
    az = (min(zs) + max(zs)) // 2
    return (ax, min_y, az)


def convert_mts(
    mts_path: Path,
    prefab_name: str,
    *,
    skip: set[str],
    mappings: dict[str, str],
    unknown: str,
    meta: dict[str, Any] | None = None,
) -> tuple[dict[str, Any], ImportReport]:
    _, _, nodes = read_mts(mts_path)
    report = ImportReport(source=str(mts_path), prefab_name=prefab_name)
    solid_positions: list[tuple[int, int, int, str]] = []

    for node in nodes:
        block = resolve_block(node.name, skip, mappings, unknown)
        if block is None:
            if node.name not in skip and node.name not in ("air", "ignore"):
                report.unmapped_nodes.append(node.name)
            continue
        report.mapped += 1
        solid_positions.append((node.x, node.y, node.z, block))

    report.total_solid = len(solid_positions)
    unique_blocks = {(x, y, z): b for x, y, z, b in solid_positions}
    anchor_nodes = [(x, y, z, False) for (x, y, z), _ in unique_blocks.items()]
    forced = [
        (n.x, n.y, n.z, True)
        for n in nodes
        if n.force and resolve_block(n.name, skip, mappings, unknown)
    ]
    ax, ay, az = compute_anchor(forced or anchor_nodes)

    blocks = []
    for (x, y, z), block in sorted(unique_blocks.items()):
        blocks.append(
            {
                "dx": x - ax,
                "dy": y - ay,
                "dz": z - az,
                "type": block,
            }
        )
    report.block_count = len(blocks)

    meta = meta or {}
    placement = meta.get("placement") or {}
    prefab: dict[str, Any] = {
        "name": prefab_name,
        "version": 2,
        "category": meta.get("category", "misc"),
        "displayName": meta.get("displayName", prefab_name.replace("_", " ").title()),
        "anchor": [0, 0, 0],
        "blocks": blocks,
    }
    if meta.get("source"):
        prefab["source"] = meta["source"]
    if placement.get("y_offset") is not None:
        prefab["placement"] = {"y_offset": int(placement["y_offset"])}

    return prefab, report


def write_prefab(path: Path, prefab: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(prefab, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, help="Single .mts file")
    parser.add_argument("--output", type=Path, help="Output .json file")
    parser.add_argument("--name", help="Prefab name")
    parser.add_argument("--batch", action="store_true")
    parser.add_argument("--input-dir", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--manifest", type=Path, default=REPO / "tools" / "prefab_manifest.yaml")
    parser.add_argument("--report", type=Path, default=REPO / "tools" / "reports")
    args = parser.parse_args()

    skip, mappings, unknown = load_node_map()
    manifest = load_manifest(args.manifest)
    known_blocks = load_known_block_names()
    reports: list[dict[str, Any]] = []

    def process_one(mts: Path, out: Path, name: str) -> int:
        meta = manifest_entry(manifest, name)
        if meta.get("import_status") == "skip":
            print(f"skip {name} (manifest)")
            return 0
        try:
            prefab, report = convert_mts(mts, name, skip=skip, mappings=mappings, unknown=unknown, meta=meta)
        except Exception as exc:
            print(f"ERROR {mts}: {exc}", file=sys.stderr)
            reports.append({"source": str(mts), "prefab": name, "error": str(exc)})
            return 1
        unknown_types = {b["type"] for b in prefab["blocks"] if b["type"] not in known_blocks}
        if unknown_types:
            report.unmapped_nodes.extend(sorted(unknown_types))
        if report.unmapped_ratio > 0.05 and meta.get("import_status") != "needs_review":
            print(f"WARN {name}: unmapped ratio {report.unmapped_ratio:.1%}")
        if not prefab["blocks"]:
            print(f"ERROR {name}: empty prefab", file=sys.stderr)
            return 1
        write_prefab(out, prefab)
        reports.append(
            {
                "source": str(mts),
                "prefab": name,
                "output": str(out),
                "block_count": report.block_count,
                "unmapped": sorted(set(report.unmapped_nodes)),
                "unmapped_ratio": round(report.unmapped_ratio, 4),
            }
        )
        print(f"OK {name} -> {out} ({report.block_count} blocks)")
        return 0

    rc = 0
    if args.batch:
        if not args.input_dir or not args.output_dir:
            parser.error("--batch requires --input-dir and --output-dir")
        prefabs = manifest.get("prefabs") or {}
        schematic_root = args.input_dir or (REPO / "third_party" / "schematics")
        for name, meta in prefabs.items():
            src_rel = meta.get("source_mts")
            if not src_rel:
                continue
            mts = Path(src_rel) if Path(src_rel).is_absolute() else schematic_root / src_rel
            if not mts.is_file():
                print(f"MISSING {name}: {src_rel}", file=sys.stderr)
                rc = 1
                continue
            out = args.output_dir / f"{name}.json"
            rc |= process_one(mts, out, name)
    elif args.input and args.output and args.name:
        rc = process_one(args.input, args.output, args.name)
    else:
        parser.error("Provide --input/--output/--name or --batch")

    if reports:
        args.report.mkdir(parents=True, exist_ok=True)
        report_path = args.report / "prefab_import_latest.json"
        report_path.write_text(json.dumps(reports, indent=2) + "\n", encoding="utf-8")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
