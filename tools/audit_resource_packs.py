#!/usr/bin/env python3
"""Audit Cubatarium resource packs against canonical_blocks.yaml."""

from __future__ import annotations

import argparse
import fnmatch
import json
import struct
import zlib
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import yaml

REPO = Path(__file__).resolve().parents[1]
PACKS_DIR = REPO / "resource_packs"
CANONICAL_PATH = Path(__file__).resolve().parent / "canonical_blocks.yaml"
AUDIT_MD = REPO / "docs" / "block-semantics-audit.md"
AUDIT_JSON = REPO / "audit_report.json"
FACE_COUNT = 6


def name_matches_patterns(name: str, patterns: list[str]) -> bool:
    lower = name.lower()
    for pattern in patterns:
        if isinstance(pattern, str) and fnmatch.fnmatch(lower, pattern.lower()):
            return True
    return False


def name_suggests_transparent(name: str, patterns: list[str]) -> bool:
    return name_matches_patterns(name, patterns)


def block_occupancy(block: dict) -> float | None:
    physics = block.get("physics")
    if not isinstance(physics, dict):
        return None
    movement = physics.get("movement")
    if not isinstance(movement, dict):
        return None
    occ = movement.get("occupancy")
    if isinstance(occ, (int, float)):
        return float(occ)
    return None


def render_style(block: dict) -> str | None:
    render = block.get("render")
    if not isinstance(render, dict):
        return None
    style = render.get("style")
    return style if isinstance(style, str) else None


def is_blend_cube(block: dict) -> bool:
    render = block.get("render")
    if not isinstance(render, dict) or render.get("transparent") is not True:
        return False
    style = render.get("style")
    return style not in ("cross", "fluid", "cutout")


def check_blend_block_marking(
    block_by_name: dict[str, dict],
    patterns: list[str],
) -> list[str]:
    warnings: list[str] = []
    if not patterns:
        return warnings
    for name, block in block_by_name.items():
        if render_style(block) == "cutout":
            continue
        transparent = isinstance(block.get("render"), dict) and block["render"].get(
            "transparent"
        ) is True
        if name_matches_patterns(name, patterns) and not transparent:
            warnings.append(
                f"{name}: name matches blend_name_patterns but render.transparent is missing"
            )
    return warnings


def check_cutout_block_marking(
    block_by_name: dict[str, dict],
    patterns: list[str],
) -> list[str]:
    warnings: list[str] = []
    if not patterns:
        return warnings
    for name, block in block_by_name.items():
        style = render_style(block)
        if style in ("cross", "fluid"):
            continue
        if name_matches_patterns(name, patterns) and style != "cutout":
            warnings.append(
                f"{name}: name matches cutout_name_patterns but render.style is not cutout"
            )
    return warnings


def check_cube_blend_mismatch(block_by_name: dict[str, dict]) -> list[str]:
    issues: list[str] = []
    for name, block in block_by_name.items():
        if not is_blend_cube(block):
            continue
        if name_matches_patterns(name, ["*leaves*", "web"]):
            issues.append(
                f"{name}: cube blend render on alpha-hole block; use render.style: cutout"
            )
    return issues


def check_cutout_occupancy(block_by_name: dict[str, dict]) -> list[str]:
    warnings: list[str] = []
    for name, block in block_by_name.items():
        if render_style(block) != "cutout":
            continue
        occ = block_occupancy(block)
        if occ is None or occ >= 1.0:
            warnings.append(
                f"{name}: cutout block should have physics.movement.occupancy < 1"
            )
    return warnings


def scan_png_alpha_profile(png_path: Path) -> tuple[bool, bool] | None:
    """Return (has_alpha_holes, has_semitransparent) for RGBA/GA PNGs."""
    try:
        raw = png_path.read_bytes()
        if raw[:8] != b"\x89PNG\r\n\x1a\n":
            return None
        if len(raw) < 26:
            return None
        width = struct.unpack(">I", raw[16:20])[0]
        height = struct.unpack(">I", raw[20:24])[0]
        bit_depth = raw[24]
        color_type = raw[25]
        if bit_depth != 8 or color_type not in (4, 6):
            return None
        bytes_per_pixel = 2 if color_type == 4 else 4
        alpha_index = 1 if color_type == 4 else 3
        pos = 8
        idat = b""
        while pos < len(raw):
            length = struct.unpack(">I", raw[pos : pos + 4])[0]
            tag = raw[pos + 4 : pos + 8]
            data = raw[pos + 8 : pos + 8 + length]
            pos += 12 + length
            if tag == b"IDAT":
                idat += data
            elif tag == b"IEND":
                break
        if not idat or width == 0 or height == 0:
            return None
        inflated = zlib.decompress(idat)
        row_bytes = 1 + width * bytes_per_pixel
        has_holes = False
        has_semi = False
        offset = 0
        for _ in range(height):
            if offset + row_bytes > len(inflated):
                return None
            row_start = offset + 1
            offset += row_bytes
            for px in range(width):
                alpha = inflated[row_start + px * bytes_per_pixel + alpha_index]
                if alpha == 0:
                    has_holes = True
                elif alpha < 255:
                    has_semi = True
        return has_holes, has_semi
    except (OSError, struct.error, zlib.error):
        return None


def check_alpha_cutout_candidates(
    pack_dir: Path,
    block_by_name: dict[str, dict],
    cutout_patterns: list[str],
) -> list[str]:
    warnings: list[str] = []
    if not cutout_patterns:
        return warnings
    tex_dir = pack_dir / "textures" / "blocks"
    for name, block in block_by_name.items():
        if not name_matches_patterns(name, cutout_patterns):
            continue
        if render_style(block) == "cutout":
            continue
        style = render_style(block)
        render = block.get("render")
        if isinstance(render, dict) and (
            render.get("transparent") is True or style in ("cross", "fluid")
        ):
            continue
        textures = block.get("textures", [])
        if not isinstance(textures, list):
            continue
        for stem in textures[:FACE_COUNT]:
            if not isinstance(stem, str):
                continue
            png = tex_dir / f"{stem}.png"
            if not png.is_file():
                continue
            profile = scan_png_alpha_profile(png)
            if profile is None:
                continue
            has_holes, has_semi = profile
            if has_holes:
                warnings.append(
                    f"{name}: {stem}.png has transparent pixels (alpha=0) but block "
                    f"has no render.style cutout or render.transparent"
                )
                break
    return warnings


def check_transparent_block_marking(
    block_by_name: dict[str, dict],
    patterns: list[str],
) -> list[str]:
    return check_blend_block_marking(block_by_name, patterns)


def load_canonical(path: Path = CANONICAL_PATH) -> dict[str, Any]:
    data = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
    return data


def read_png_first_pixel(png_path: Path) -> tuple[int, int, int, int] | None:
    try:
        raw = png_path.read_bytes()
        if raw[:8] != b"\x89PNG\r\n\x1a\n":
            return None
        pos = 8
        idat = b""
        while pos < len(raw):
            length = struct.unpack(">I", raw[pos : pos + 4])[0]
            tag = raw[pos + 4 : pos + 8]
            data = raw[pos + 8 : pos + 8 + length]
            pos += 12 + length
            if tag == b"IDAT":
                idat += data
            elif tag == b"IEND":
                break
        if not idat:
            return None
        inflated = zlib.decompress(idat)
        if len(inflated) < 5:
            return None
        return tuple(inflated[1:5])  # type: ignore[return-value]
    except (OSError, struct.error, zlib.error):
        return None


PLACEHOLDER_RGBA = (160, 160, 160, 160)


def is_placeholder_texture(png_path: Path) -> bool:
    px = read_png_first_pixel(png_path)
    return px == PLACEHOLDER_RGBA


def check_block_texture_stems(
    pack_dir: Path,
    block_by_name: dict[str, dict],
) -> list[str]:
    issues: list[str] = []
    tex_dir = pack_dir / "textures" / "blocks"
    for block_name, block in block_by_name.items():
        textures = block.get("textures", [])
        if not isinstance(textures, list):
            continue
        for stem in textures[:FACE_COUNT]:
            if not isinstance(stem, str):
                continue
            png = tex_dir / f"{stem}.png"
            if not png.is_file():
                issues.append(f"{block_name}: missing texture {stem}.png")
                continue
            if is_placeholder_texture(png):
                issues.append(f"{block_name}: placeholder texture {stem}.png")
    return issues


def read_png_size(png_path: Path) -> tuple[int, int] | None:
    try:
        raw = png_path.read_bytes()
        if raw[:8] != b"\x89PNG\r\n\x1a\n":
            return None
        w = struct.unpack(">I", raw[16:20])[0]
        h = struct.unpack(">I", raw[20:24])[0]
        return w, h
    except OSError:
        return None


def stem_matches_denylist(stem: str, patterns: list[str]) -> str | None:
    lower = stem.lower()
    for pattern in patterns:
        if fnmatch.fnmatch(lower, pattern.lower()):
            return pattern
    return None


def nested_get(data: dict | None, *keys: str) -> Any:
    cur: Any = data
    for key in keys:
        if not isinstance(cur, dict):
            return None
        cur = cur.get(key)
    return cur


def types_ok(actual: list | None, expected: list | None) -> bool:
    if not expected:
        return True
    if not actual:
        return False
    return set(expected).issubset(set(actual))


def semantics_match(block: dict, spec: dict) -> list[str]:
    issues: list[str] = []
    exp_physics = spec.get("physics")
    act_physics = block.get("physics")
    if exp_physics:
        if nested_get(act_physics, "preset") != nested_get(exp_physics, "preset"):
            issues.append(
                f"physics.preset expected {nested_get(exp_physics, 'preset')!r}, "
                f"got {nested_get(act_physics, 'preset')!r}"
            )
        exp_occ = nested_get(exp_physics, "movement", "occupancy")
        act_occ = nested_get(act_physics, "movement", "occupancy")
        if exp_occ is not None and act_occ != exp_occ:
            issues.append(
                f"physics.movement.occupancy expected {exp_occ!r}, got {act_occ!r}"
            )
    exp_render = spec.get("render")
    act_render = block.get("render")
    if exp_render:
        for key in ("transparent", "style"):
            if key in exp_render and nested_get(act_render, key) != exp_render[key]:
                issues.append(
                    f"render.{key} expected {exp_render[key]!r}, got {nested_get(act_render, key)!r}"
                )
    exp_types = spec.get("types")
    act_types = block.get("types")
    if exp_types and not types_ok(act_types, exp_types):
        issues.append(f"types expected superset of {exp_types!r}, got {act_types!r}")
    exp_anim = spec.get("animation")
    act_anim = block.get("animation")
    if exp_anim:
        if not act_anim:
            issues.append("missing animation section")
        else:
            for key in ("frame_count", "frametime"):
                if key in exp_anim and act_anim.get(key) != exp_anim[key]:
                    issues.append(
                        f"animation.{key} expected {exp_anim[key]!r}, got {act_anim.get(key)!r}"
                    )
    exp_display = spec.get("display_name")
    if exp_display and block.get("displayName") != exp_display:
        issues.append(
            f"displayName expected {exp_display!r}, got {block.get('displayName')!r}"
        )
    return issues


def check_animation_png(
    pack_dir: Path,
    block: dict,
    block_name: str,
) -> list[str]:
    issues: list[str] = []
    animation = block.get("animation")
    if not animation:
        return issues
    frame_count = animation.get("frame_count")
    if not isinstance(frame_count, int) or frame_count < 1:
        issues.append(f"{block_name}: invalid animation.frame_count")
        return issues
    textures = block.get("textures", [])
    if not isinstance(textures, list) or not textures:
        return issues
    tex_dir = pack_dir / "textures" / "blocks"
    if len(textures) == FACE_COUNT:
        stems = textures[:FACE_COUNT]
        for stem in stems:
            if not isinstance(stem, str):
                continue
            png = tex_dir / f"{stem}.png"
            if not png.is_file():
                continue
            size = read_png_size(png)
            if size is None:
                issues.append(f"{block_name}: could not read PNG size for {stem}")
                continue
            w, h = size
            expected_h = w * frame_count
            if h != expected_h:
                issues.append(
                    f"{block_name}: {stem}.png height {h} != width×frames ({w}×{frame_count}={expected_h})"
                )
    elif len(textures) > FACE_COUNT and len(textures) % FACE_COUNT == 0:
        layer_frames = len(textures) // FACE_COUNT
        if layer_frames != frame_count:
            issues.append(
                f"{block_name}: texture layers {layer_frames} != animation.frame_count {frame_count}"
            )
        for stem in textures[:FACE_COUNT]:
            if not isinstance(stem, str):
                continue
            png = tex_dir / f"{stem}.png"
            if not png.is_file():
                continue
            size = read_png_size(png)
            if size is None:
                issues.append(f"{block_name}: could not read PNG size for {stem}")
                continue
            w, h = size
            if h != w:
                issues.append(
                    f"{block_name}: {stem}.png must be square for layer animation ({w}×{h})"
                )
    return issues


def load_block_json(path: Path) -> dict | None:
    try:
        return json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError):
        return None


def audit_pack(
    pack_dir: Path,
    canonical: dict[str, Any],
    installed_ids: set[str],
    reference_blocks: dict[str, dict] | None = None,
) -> dict[str, Any]:
    pack_json_path = pack_dir / "pack.json"
    manifest: dict[str, Any] = {}
    if pack_json_path.is_file():
        try:
            manifest = json.loads(pack_json_path.read_text(encoding="utf-8-sig"))
        except json.JSONDecodeError:
            pass
    pack_id = manifest.get("id", pack_dir.name)
    declared_role = manifest.get("worldgen_role", "secondary")
    tier_a: dict[str, Any] = canonical.get("tier_a", {})
    global_deny: list[str] = canonical.get("global_stem_denylist", [])

    issues: list[str] = []
    warnings: list[str] = []
    tier_present = 0
    tier_semantics_ok = 0

    blocks_dir = pack_dir / "blocks"
    block_by_name: dict[str, dict] = {}
    if blocks_dir.is_dir():
        for block_path in sorted(blocks_dir.glob("*.json")):
            data = load_block_json(block_path)
            if data and data.get("name"):
                block_by_name[data["name"]] = data

    for name, spec in tier_a.items():
        block = block_by_name.get(name)
        if block is None:
            issues.append(f"missing tier_a block: {name}")
            continue
        tier_present += 1

        sem_issues = semantics_match(block, spec)
        deny_patterns = list(global_deny) + list(spec.get("stem_denylist", []))
        textures = block.get("textures", [])
        if isinstance(textures, list):
            for stem in textures:
                if not isinstance(stem, str):
                    continue
                hit = stem_matches_denylist(stem, deny_patterns)
                if hit:
                    sem_issues.append(f"denylisted stem {stem!r} (pattern {hit!r})")
        sem_issues.extend(check_animation_png(pack_dir, block, name))

        if sem_issues:
            for msg in sem_issues:
                issues.append(f"{name}: {msg}")
        else:
            tier_semantics_ok += 1

        if reference_blocks is not None and name in reference_blocks:
            ref_tex = reference_blocks[name].get("textures")
            act_tex = block.get("textures")
            if ref_tex != act_tex:
                warnings.append(
                    f"{name}: textures differ from reference pack "
                    f"(ref={ref_tex!r}, actual={act_tex!r})"
                )

    tier_total = len(tier_a)
    score = round(100.0 * tier_semantics_ok / tier_total, 1) if tier_total else 0.0
    worldgen_capable = (
        tier_present == tier_total and tier_semantics_ok == tier_total
    )

    if declared_role == "primary" and not worldgen_capable:
        warnings.append(
            f"declared primary but tier A incomplete "
            f"({tier_present}/{tier_total} present, "
            f"{tier_semantics_ok}/{tier_total} semantics OK)"
        )

    depends = manifest.get("depends", [])
    if isinstance(depends, list):
        for dep in depends:
            if dep not in installed_ids:
                issues.append(f"missing depends pack: {dep}")

    conflicts = manifest.get("conflicts", [])
    if isinstance(conflicts, list):
        for other in conflicts:
            if other in installed_ids and other != pack_id:
                warnings.append(f"conflicts with installed pack: {other}")

    resolution = manifest.get("resolution")
    if resolution is not None and not isinstance(resolution, int):
        warnings.append(f"non-integer resolution: {resolution!r}")

    for msg in check_block_texture_stems(pack_dir, block_by_name):
        issues.append(msg)

    for msg in check_cube_blend_mismatch(block_by_name):
        issues.append(msg)

    blend_patterns = canonical.get("blend_name_patterns", [])
    if isinstance(blend_patterns, list):
        warnings.extend(check_blend_block_marking(block_by_name, blend_patterns))

    cutout_patterns = canonical.get("cutout_name_patterns", [])
    if isinstance(cutout_patterns, list):
        warnings.extend(check_cutout_block_marking(block_by_name, cutout_patterns))

    warnings.extend(check_cutout_occupancy(block_by_name))
    if isinstance(cutout_patterns, list):
        warnings.extend(
            check_alpha_cutout_candidates(pack_dir, block_by_name, cutout_patterns)
        )

    transparent_patterns = canonical.get("transparent_name_patterns", [])
    if isinstance(transparent_patterns, list) and transparent_patterns:
        warnings.extend(
            check_transparent_block_marking(block_by_name, transparent_patterns)
        )

    return {
        "pack_id": pack_id,
        "pack_dir": str(pack_dir.relative_to(REPO)).replace("\\", "/"),
        "tier_a_present": tier_present,
        "tier_a_total": tier_total,
        "tier_a_semantics_ok": tier_semantics_ok,
        "worldgen_ready_score": score,
        "worldgen_capable": worldgen_capable,
        "worldgen_role": declared_role,
        "declared_role": declared_role,
        "resolution": resolution,
        "depends": depends if isinstance(depends, list) else [],
        "conflicts": conflicts if isinstance(conflicts, list) else [],
        "issues": issues,
        "warnings": warnings,
        "block_count": len(block_by_name),
    }


def load_pack_manifest(pack_dir: Path) -> dict[str, Any]:
    pack_json_path = pack_dir / "pack.json"
    if not pack_json_path.is_file():
        return {}
    try:
        return json.loads(pack_json_path.read_text(encoding="utf-8-sig"))
    except json.JSONDecodeError:
        return {}


def is_declared_primary(pack_dir: Path) -> bool:
    manifest = load_pack_manifest(pack_dir)
    return manifest.get("worldgen_role") == "primary"


def write_pack_roles(pack_dir: Path, role: str) -> None:
    pack_json_path = pack_dir / "pack.json"
    if not pack_json_path.is_file():
        return
    manifest = json.loads(pack_json_path.read_text(encoding="utf-8-sig"))
    manifest["worldgen_role"] = role
    manifest["pack_format"] = 1
    pack_json_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def load_role_policy() -> dict[str, str]:
    policy_path = REPO / "tools" / "pack_dependencies.yaml"
    if not policy_path.is_file():
        return {}
    try:
        import yaml
    except ImportError:
        return {}
    data = yaml.safe_load(policy_path.read_text(encoding="utf-8")) or {}
    packs = data.get("packs", {})
    roles: dict[str, str] = {}
    if isinstance(packs, dict):
        for pack_id, spec in packs.items():
            if isinstance(spec, dict):
                role = spec.get("worldgen_role", "secondary")
                if isinstance(role, str):
                    roles[pack_id] = role
    return roles


def render_markdown(report: dict[str, Any]) -> str:
    lines = [
        "# Block semantics audit",
        "",
        f"Generated: {report['generated_at']}",
        "",
        "## Summary",
        "",
        f"| Pack | Blocks | Tier A | Semantics OK | Score | Role |",
        f"|------|--------|--------|--------------|-------|------|",
    ]
    for pack in report["packs"]:
        lines.append(
            f"| `{pack['pack_id']}` | {pack['block_count']} | "
            f"{pack['tier_a_present']}/{pack['tier_a_total']} | "
            f"{pack['tier_a_semantics_ok']}/{pack['tier_a_total']} | "
            f"{pack['worldgen_ready_score']}% | {pack['worldgen_role']} |"
        )
    global_warnings = report.get("global_warnings", [])
    if global_warnings:
        lines.extend(["", "## Global warnings", ""])
        for w in global_warnings:
            lines.append(f"- {w}")
    lines.extend(["", "## Per-pack issues", ""])
    for pack in report["packs"]:
        if not pack["issues"] and not pack["warnings"]:
            continue
        lines.append(f"### `{pack['pack_id']}`")
        lines.append("")
        if pack["issues"]:
            lines.append("**Errors / semantics:**")
            lines.append("")
            for issue in pack["issues"]:
                lines.append(f"- {issue}")
            lines.append("")
        if pack["warnings"]:
            lines.append("**Warnings:**")
            lines.append("")
            for warn in pack["warnings"]:
                lines.append(f"- {warn}")
            lines.append("")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description="Audit resource packs against canonical_blocks.yaml")
    parser.add_argument(
        "--packs-dir",
        type=Path,
        default=PACKS_DIR,
        help="Directory containing resource pack folders",
    )
    parser.add_argument(
        "--write-roles",
        action="store_true",
        help="Write worldgen_role and pack_format:1 to each pack.json",
    )
    parser.add_argument(
        "--canonical",
        type=Path,
        default=CANONICAL_PATH,
        help="Path to canonical_blocks.yaml",
    )
    parser.add_argument(
        "--output-md",
        type=Path,
        default=AUDIT_MD,
        help="Markdown report output path",
    )
    parser.add_argument(
        "--reference-pack",
        type=str,
        default=None,
        help="Pack id whose tier_a textures are compared as reference (warnings only)",
    )
    parser.add_argument(
        "--primary-only",
        action="store_true",
        help="Audit only packs with worldgen_role: primary in pack.json",
    )
    parser.add_argument(
        "--fail-on-warnings",
        action="store_true",
        help="Exit with error if any pack has warnings",
    )
    parser.add_argument(
        "--output-json",
        type=Path,
        default=AUDIT_JSON,
        help="JSON report output path",
    )
    args = parser.parse_args()

    canonical = load_canonical(args.canonical.resolve())
    packs_dir = args.packs_dir.resolve()
    if not packs_dir.is_dir():
        print(f"ERROR: packs dir not found: {packs_dir}")
        return 1

    pack_dirs = sorted(p for p in packs_dir.iterdir() if p.is_dir() and (p / "pack.json").is_file())
    if args.primary_only:
        pack_dirs = [p for p in pack_dirs if is_declared_primary(p)]
        if not pack_dirs:
            print("ERROR: no primary packs found")
            return 1
        print(f"Primary-only audit: {len(pack_dirs)} pack(s)")
    installed_ids = set()
    for p in pack_dirs:
        try:
            m = json.loads((p / "pack.json").read_text(encoding="utf-8-sig"))
            installed_ids.add(m.get("id", p.name))
        except json.JSONDecodeError:
            installed_ids.add(p.name)

    reference_blocks: dict[str, dict] | None = None
    if args.reference_pack:
        ref_dir = packs_dir / args.reference_pack
        if not ref_dir.is_dir():
            print(f"ERROR: reference pack not found: {ref_dir}")
            return 1
        reference_blocks = {}
        blocks_dir = ref_dir / "blocks"
        if blocks_dir.is_dir():
            for block_path in blocks_dir.glob("*.json"):
                data = load_block_json(block_path)
                if data and data.get("name"):
                    reference_blocks[data["name"]] = data
        print(f"Reference pack '{args.reference_pack}': {len(reference_blocks)} blocks")

    pack_results = [
        audit_pack(p, canonical, installed_ids, reference_blocks) for p in pack_dirs
    ]

    global_warnings: list[str] = []
    resolutions: dict[int, list[str]] = {}
    for result in pack_results:
        res = result.get("resolution")
        if isinstance(res, int):
            resolutions.setdefault(res, []).append(result["pack_id"])
    if len(resolutions) > 1:
        global_warnings = [
            f"resolution_mismatch across installed packs — "
            + ", ".join(f"{res}px: {', '.join(ids)}" for res, ids in sorted(resolutions.items()))
        ]

    if args.write_roles:
        role_policy = load_role_policy()
        for pack_dir, result in zip(pack_dirs, pack_results):
            pack_id = result["pack_id"]
            role = role_policy.get(pack_id, "secondary")
            write_pack_roles(pack_dir, role)

    report = {
        "generated_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "canonical": str(args.canonical.resolve().relative_to(REPO)).replace("\\", "/"),
        "reference_pack": args.reference_pack,
        "primary_only": args.primary_only,
        "pack_count": len(pack_results),
        "primary_issues": sum(len(p["issues"]) for p in pack_results),
        "global_warnings": global_warnings,
        "packs": pack_results,
    }

    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_md.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    args.output_md.write_text(render_markdown(report), encoding="utf-8")

    print(f"Wrote {args.output_json.relative_to(REPO)}")
    print(f"Wrote {args.output_md.relative_to(REPO)}")
    if args.write_roles:
        print("Updated pack.json worldgen_role and pack_format in all packs")

    error_count = sum(len(p["issues"]) for p in pack_results)
    warn_count = len(global_warnings) + sum(len(p["warnings"]) for p in pack_results)
    label = "primary " if args.primary_only else ""
    print(f"Audit complete: {error_count} {label}issue(s), {warn_count} warning(s)")
    if error_count:
        return 1
    if args.fail_on_warnings and warn_count:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
