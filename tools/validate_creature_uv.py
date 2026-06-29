#!/usr/bin/env python3
"""UV quality metrics for rigid_voxels creature textures."""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))

from PIL import Image

from box_uv_layout import FACE_ORDER, face_pixel_rect, unfold_canvas_size
from creature_tier_a import TIER_A_MOBS
from creature_uv_common import (
    GATE_IDS,
    MODELS,
    RESEARCH_DEFAULT,
    gate_skips,
    has_snout_part,
    is_placeholder,
    list_all_species,
    load_creature,
    load_sources,
    load_thresholds,
    profile_thresholds,
    texture_stems,
    threshold_profile,
    upstream_exists,
    WAVE_MAP,
    WAVE_SPECIES,
)

TEXELS_PER_BLOCK = 16
UV_PAD_PX = 1

PREVIEW_BASELINE_DIR = TOOLS / "uv_preview_baseline"
PREVIEW_CURRENT_DIR = ROOT / "bin" / "uv_preview"


def icon_gate_status(species_id: str) -> str:
    from audit_creature_catalog import icon_quality

    iq = icon_quality(MODELS / species_id)
    if iq in ("solid_color", "missing"):
        return "fail"
    return "pass"


def preview_gate_status(species_id: str, max_hamming: int = 12) -> str:
    from compare_creature_preview import compare_dir

    baseline = PREVIEW_BASELINE_DIR / species_id
    if not baseline.is_dir():
        return "skip"
    current = PREVIEW_CURRENT_DIR / species_id
    if not current.is_dir():
        return "skip"
    ok, _ = compare_dir(species_id, current, baseline, max_hamming)
    return "pass" if ok else "fail"


def rgb_to_lab(r: int, g: int, b: int) -> tuple[float, float, float]:
    def f(c: float) -> float:
        c /= 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4

    r, g, b = f(r), f(g), f(b)
    x = r * 0.4124 + g * 0.3576 + b * 0.1805
    y = r * 0.2126 + g * 0.7152 + b * 0.0722
    z = r * 0.0193 + g * 0.1192 + b * 0.9505
    x, y, z = x / 0.95047, y / 1.0, z / 1.08883

    def f2(t: float) -> float:
        return t ** (1 / 3) if t > 0.008856 else 7.787 * t + 16 / 116

    fx, fy, fz = f2(x), f2(y), f2(z)
    return 116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz)


def delta_e(a: tuple[int, int, int], b: tuple[int, int, int]) -> float:
    la, aa, ba = rgb_to_lab(*a)
    lb, ab, bb = rgb_to_lab(*b)
    return math.sqrt((la - lb) ** 2 + (aa - ab) ** 2 + (ba - bb) ** 2)


def alpha_fraction(img: Image.Image) -> float:
    px = list(img.convert("RGBA").getdata())
    if not px:
        return 0.0
    return sum(1 for *_, a in px if a < 250) / len(px)


def opaque_fraction(img: Image.Image) -> float:
    px = list(img.convert("RGBA").getdata())
    if not px:
        return 0.0
    return sum(1 for *_, a in px if a >= 250) / len(px)


def face_coverages(
    img: Image.Image, sx: float, sy: float, sz: float
) -> dict[str, float]:
    out: dict[str, float] = {}
    for face in FACE_ORDER:
        x0, y0, x1, y1 = face_pixel_rect(face, sx, sy, sz, UV_PAD_PX, TEXELS_PER_BLOCK)
        crop = img.crop((x0, y0, x1 + 1, y1 + 1))
        out[face] = opaque_fraction(crop)
    return out


def mean_face_color(img: Image.Image, face: str, sx: float, sy: float, sz: float) -> tuple[int, int, int] | None:
    x0, y0, x1, y1 = face_pixel_rect(face, sx, sy, sz, UV_PAD_PX, TEXELS_PER_BLOCK)
    crop = img.crop((x0, y0, x1 + 1, y1 + 1)).convert("RGBA")
    rs, gs, bs, n = 0, 0, 0, 0
    for r, g, b, a in crop.getdata():
        if a >= 250:
            rs += r
            gs += g
            bs += b
            n += 1
    if n == 0:
        return None
    return rs // n, gs // n, bs // n


def snout_front_score(img: Image.Image, sx: float, sy: float, sz: float) -> float:
    cov = face_coverages(img, sx, sy, sz)
    front = cov.get("pz", 0.0)
    others = [v for k, v in cov.items() if k != "pz"]
    mean_other = sum(others) / max(1, len(others))
    if mean_other < 1e-6:
        return front * 100.0
    return front / mean_other


def b3d_assignment_rate(species_id: str, research: Path) -> float | None:
    sources = load_sources()
    spec = (sources.get("species") or {}).get(species_id) or {}
    model = spec.get("model")
    if not model:
        return None
    from bake_rigid_creature_textures import assign_vertex_parts, mesh_bounds, vertex_to_block
    from b3d_read import load_b3d_vertices

    creature = load_creature(species_id)
    verts = load_b3d_vertices(research / model)
    if not verts:
        return 0.0
    bounds = mesh_bounds(verts)
    rest = creature["bounds"]["rest"]
    part_defs = creature["visual"]["parts"]
    maps = __import__("yaml").safe_load((TOOLS / "creature_rigid_uv_maps.yaml").read_text(encoding="utf-8"))
    match_margin = float(maps.get("match_margin", 0.08))
    leg_margin = float(maps.get("leg_margin", 0.12))
    assignments = assign_vertex_parts(verts, bounds, rest, part_defs, match_margin, leg_margin)
    return len(assignments) / len(verts)


def legacy_fallback_used(species_id: str) -> bool:
    log_path = ROOT / "bin" / "bake_log.txt"
    if not log_path.is_file():
        return False
    text = log_path.read_text(encoding="utf-8", errors="ignore")
    return f"{species_id}/" in text and "legacy fallback" in text


def validate_species(species_id: str, research: Path = RESEARCH_DEFAULT) -> dict[str, Any]:
    creature = load_creature(species_id)
    visual = creature.get("visual", {})
    layout = visual.get("texture_layout", "")
    profile_name = threshold_profile(species_id, creature)
    thr = profile_thresholds(species_id, creature)
    skips = set(gate_skips(species_id, creature))
    tex_dir = MODELS / species_id / "textures"

    metrics: dict[str, Any] = {
        "profile": profile_name,
        "layout": layout,
        "placeholder": is_placeholder(species_id),
        "wave": WAVE_MAP.get(species_id, "?"),
    }
    failures: list[str] = []
    passes: list[str] = []

    if profile_name == "human":
        skips = set(gate_skips(species_id, creature))
        lic = MODELS / species_id / "LICENSE.txt"
        g10 = (
            "pass"
            if lic.is_file()
            and "Placeholder procedural" not in lic.read_text(encoding="utf-8", errors="ignore")
            else "fail"
        )
        gates: dict[str, str] = {}
        for gid in GATE_IDS:
            if gid == "G13":
                continue
            if gid in skips:
                gates[gid] = "skip"
            elif gid == "G01":
                gates[gid] = "pass" if upstream_exists(species_id, research) else "fail"
            elif gid == "G10":
                gates[gid] = g10
            elif gid == "G11":
                gates[gid] = icon_gate_status(species_id)
            elif gid == "G12":
                gates[gid] = preview_gate_status(species_id)
            else:
                gates[gid] = "skip"
        g_pass = all(
            gates.get(g, "skip") in ("pass", "skip")
            for g in GATE_IDS
            if g != "G13" and (g != "G12" or gates.get("G12") != "skip")
        )
        gates["G13"] = "pass" if g_pass else "pending"
        metrics["skipped_v3"] = True
        return {
            "species": species_id,
            "metrics": metrics,
            "pass": gates.get("G13") == "pass",
            "failures": [],
            "gates": gates,
        }

    layout_ok = layout == "box_uv" or layout == "player_skin_atlas"
    metrics["layout_ok"] = layout_ok
    if "G04" not in skips and not layout_ok:
        failures.append("layout_ok")

    stems = texture_stems(creature)
    part_by_stem: dict[str, dict] = {}
    for part in visual.get("parts", []):
        stem = part.get("texture")
        if stem:
            part_by_stem[stem] = part

    png_dims_ok = True
    uv_sidecar_ok = True
    opaque_ok = True
    face_cov_all: dict[str, dict[str, float]] = {}
    face_bleed_scores: list[float] = []
    snout_scores: list[float] = []
    any_manual_fallback = False

    if layout == "box_uv":
        checked_stems: set[str] = set()
        for part in visual.get("parts", []):
            stem = part.get("texture")
            if not stem or stem in checked_stems:
                continue
            checked_stems.add(stem)
            png = tex_dir / f"{stem}.png"
            if not png.is_file():
                png_dims_ok = False
                uv_sidecar_ok = False
                opaque_ok = False
                failures.append(f"missing_png:{stem}")
                continue
            sx, sy, sz = part["size"]
            ew, eh = unfold_canvas_size(sx, sy, sz, UV_PAD_PX, TEXELS_PER_BLOCK)
            img = Image.open(png)
            if img.size != (ew, eh):
                png_dims_ok = False
                failures.append(f"png_dims:{stem}")
            sidecar = tex_dir / f"{stem}.uv.json"
            manual_fb = False
            if sidecar.is_file():
                import json as _json

                sc = _json.loads(sidecar.read_text(encoding="utf-8"))
                manual_fb = bool(sc.get("manual_fallback"))
                any_manual_fallback = any_manual_fallback or manual_fb
            else:
                uv_sidecar_ok = False
                failures.append(f"sidecar:{stem}")
            op = opaque_fraction(img)
            if op < thr.get("opaque_frac_min", 0.1):
                opaque_ok = False
                failures.append(f"opaque:{stem}")
            cov = face_coverages(img, sx, sy, sz)
            face_cov_all[stem] = cov
            good_faces = sum(1 for v in cov.values() if v >= thr.get("face_coverage_min", 0.7))
            if good_faces < thr.get("face_coverage_faces_min", 4):
                failures.append(f"face_coverage:{stem}")
            c_front = mean_face_color(img, "pz", sx, sy, sz)
            c_side = mean_face_color(img, "px", sx, sy, sz)
            if c_front and c_side and not manual_fb:
                face_bleed_scores.append(delta_e(c_front, c_side))
            if part.get("id") in ("snout", "beak"):
                snout_scores.append(snout_front_score(img, sx, sy, sz))

    metrics["png_dims_ok"] = png_dims_ok
    metrics["uv_sidecar_present"] = uv_sidecar_ok
    metrics["opaque_frac_ok"] = opaque_ok
    metrics["face_coverage"] = face_cov_all
    metrics["manual_fallback"] = any_manual_fallback
    if face_bleed_scores and not any_manual_fallback:
        metrics["face_bleed_score"] = sum(face_bleed_scores) / len(face_bleed_scores)
        if metrics["face_bleed_score"] < thr.get("face_bleed_min", 10.0):
            failures.append("face_bleed_score")
    if snout_scores:
        metrics["snout_front_score"] = max(snout_scores)
        min_ratio = thr.get("snout_front_ratio_min", 1.5)
        if min_ratio > 0 and metrics["snout_front_score"] < min_ratio:
            failures.append("snout_front_score")

    rate = b3d_assignment_rate(species_id, research)
    if rate is not None:
        metrics["b3d_assignment_rate"] = rate
        if "G03" not in skips and rate < thr.get("b3d_assignment_rate_min", 0.75):
            failures.append("b3d_assignment_rate")

    legacy = legacy_fallback_used(species_id)
    metrics["legacy_fallback_used"] = legacy
    if legacy:
        failures.append("legacy_fallback_used")

    hard_failures = [
        f
        for f in failures
        if not f.startswith(
            ("face_bleed", "b3d_assignment", "snout_front", "face_coverage", "opaque")
        )
    ]
    metrics["hard_failures"] = hard_failures

    gates: dict[str, str] = {}
    if "G01" not in skips:
        gates["G01"] = "pass" if upstream_exists(species_id, research) else "fail"
    else:
        gates["G01"] = "skip"
    gates["G02"] = "skip" if "G02" in skips else ("pass" if rate is not None else "fail")
    gates["G03"] = "skip" if "G03" in skips else "pass"
    gates["G04"] = "skip" if "G04" in skips else ("pass" if layout_ok else "fail")
    gates["G05"] = "skip" if "G05" in skips else ("fail" if legacy else "pass")
    gate_metric_fail = bool(hard_failures) or any(
        f.startswith(("png_dims", "missing_png", "sidecar")) for f in failures
    )
    gates["G06"] = "skip" if "G06" in skips else ("fail" if gate_metric_fail else "pass")
    gates["G07"] = "skip" if "G07" in skips else "pass"
    gates["G08"] = "skip" if "G08" in skips else ("pass" if uv_sidecar_ok and png_dims_ok else "fail")
    gates["G09"] = "skip" if "G09" in skips else "pass"
    lic = MODELS / species_id / "LICENSE.txt"
    gates["G10"] = "pass" if lic.is_file() and "Placeholder procedural" not in lic.read_text(encoding="utf-8", errors="ignore") else "fail"
    if is_placeholder(species_id) and not lic.is_file():
        gates["G10"] = "fail"
    gates["G11"] = icon_gate_status(species_id)
    gates["G12"] = preview_gate_status(
        species_id, int(thr.get("preview_max_hamming", 12))
    )
    core = ("G01", "G02", "G03", "G04", "G05", "G06", "G07", "G08", "G09", "G10", "G11")
    g_pass = all(gates.get(g, "skip") in ("pass", "skip") for g in core)
    if gates["G12"] != "skip":
        g_pass = g_pass and gates["G12"] == "pass"
    gates["G13"] = "pass" if g_pass else "pending"

    return {
        "species": species_id,
        "metrics": metrics,
        "pass": len(hard_failures) == 0 and gates.get("G13") == "pass",
        "failures": failures,
        "gates": gates,
    }


def resolve_species_list(args: argparse.Namespace) -> list[str]:
    if args.all:
        return list_all_species()
    if args.tier_a:
        return list(TIER_A_MOBS)
    if args.wave:
        return list(WAVE_SPECIES.get(args.wave, []))
    if args.species:
        return list(args.species)
    raise SystemExit("Specify --species, --wave, --tier-a, or --all")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--species", nargs="+")
    parser.add_argument("--wave")
    parser.add_argument("--tier-a", action="store_true")
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--research", type=Path, default=RESEARCH_DEFAULT)
    parser.add_argument("--write-report", action="store_true")
    parser.add_argument("--fail-on-threshold", action="store_true")
    parser.add_argument("--json-lines", action="store_true")
    args = parser.parse_args()

    species_list = resolve_species_list(args)
    report: dict[str, Any] = {}
    failures = 0
    for sid in species_list:
        result = validate_species(sid, args.research)
        report[sid] = result
        if args.json_lines:
            print(json.dumps(result, ensure_ascii=False))
        else:
            status = "PASS" if result["pass"] else "FAIL"
            print(f"{status} {sid}: failures={result['failures']} gates={result.get('gates', {})}")
        if not result["pass"]:
            failures += 1

    if args.write_report:
        out = TOOLS / "uv_quality_report.yaml"
        import yaml

        yaml.safe_dump(report, out.open("w", encoding="utf-8"), sort_keys=False, allow_unicode=True)
        print(f"wrote {out}")

    if args.fail_on_threshold and failures:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
