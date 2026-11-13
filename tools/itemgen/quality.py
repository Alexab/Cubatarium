"""Item visual quality audit (familiar silhouette / tier separation)."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

from itemgen.preview import OUT, _load_parts_from_gltf, _render, make_contact_sheet, make_family_sheet

ROOT = Path(__file__).resolve().parents[2]
MODELS = ROOT / "models" / "items"
CONTENT = ROOT / "content" / "items"
REPORT = ROOT / "docs" / "research" / "item-visual-qa-2026-11-13.md"
JSON_REPORT = ROOT / "docs" / "research" / "item-visual-qa-2026-11-13.json"

FAMILIES = ["pickaxe", "axe", "shovel", "hoe", "sword"]
MATERIALS = ["wood", "stone", "copper", "iron", "gold"]


def _silhouette_hash(item_id: str) -> str:
    parts = _load_parts_from_gltf(MODELS / item_id)
    img = _render(parts, mode="silhouette", size=96, yaw=35.0).convert("L")
    bw = img.point(lambda p: 0 if p < 200 else 255)
    return hashlib.sha1(bw.tobytes()).hexdigest()[:12]


def _gray_mean(item_id: str) -> float:
    parts = _load_parts_from_gltf(MODELS / item_id)
    img = _render(parts, mode="gray", size=96, yaw=35.0).convert("L")
    hist = img.histogram()
    # Measure item pixels only: the gray render uses a pure-white backdrop,
    # excluded here so thin tools are not drowned by background (which used to
    # compress every family's tier means into one or two buckets).
    background = hist[255]
    total = sum(hist) - background
    total = total or 1
    numerator = sum(i * hist[i] for i in range(256)) - 255 * background
    return numerator / total


def _triangle_budget(item_id: str) -> int:
    gltf = json.loads((MODELS / item_id / "model.gltf").read_text(encoding="utf-8"))
    total = 0
    for prim in gltf["meshes"][0]["primitives"]:
        acc = gltf["accessors"][prim["indices"]]
        total += acc["count"] // 3
    return total


def _has_diffuse(item_id: str) -> bool:
    return (MODELS / item_id / "textures" / "diffuse.png").is_file()


def run_audit() -> dict:
    OUT.mkdir(parents=True, exist_ok=True)
    findings: dict = {
        "families": {},
        "items": {},
        "failures": [],
        "warnings": [],
    }

    # Per-item contact sheets for the full catalog with recipes. One broken
    # item must not abort the audit: record it and keep going.
    for path in sorted(CONTENT.glob("*.json")):
        item_id = path.stem
        if not (MODELS / item_id / "model.gltf").is_file():
            findings["failures"].append(f"missing_model:{item_id}")
            continue
        if not (MODELS / item_id / "model.bin").is_file():
            findings["failures"].append(f"missing_bin:{item_id}")
            continue
        try:
            make_contact_sheet(item_id)
            tris = _triangle_budget(item_id)
            has_tex = _has_diffuse(item_id)
            entry = {
                "triangles": tris,
                "has_diffuse_png": has_tex,
                "silhouette_hash": _silhouette_hash(item_id),
                "gray_mean": round(_gray_mean(item_id), 2),
            }
        except Exception as exc:  # noqa: BLE001 - audit must always report
            findings["failures"].append(f"render_failed:{item_id}:{type(exc).__name__}")
            continue
        findings["items"][item_id] = entry
        if not has_tex:
            findings["failures"].append(f"missing_texture:{item_id}")
        if tris > 1500:
            findings["warnings"].append(f"high_tris:{item_id}:{tris}")
        if tris < 12:
            findings["warnings"].append(f"too_simple:{item_id}:{tris}")

    for family in FAMILIES:
        sheet = make_family_sheet(family, MATERIALS)
        hashes = {}
        grays = {}
        for mat in MATERIALS:
            item_id = f"{mat}_{family}"
            if item_id not in findings["items"]:
                continue
            hashes[mat] = findings["items"][item_id]["silhouette_hash"]
            grays[mat] = findings["items"][item_id]["gray_mean"]
        # Wood/stone should not share identical silhouette with forged iron.
        if "wood" in hashes and "iron" in hashes and hashes["wood"] == hashes["iron"]:
            findings["failures"].append(f"same_silhouette:{family}:wood==iron")
        # Gray means must not all collapse to one value: the gray column is the
        # colorblind tier cue, so collapsed tiers are a hard failure.
        unique_gray = len(set(int(v) for v in grays.values()))
        if unique_gray < 3 and len(grays) >= 4:
            findings["failures"].append(f"weak_gray_separation:{family}:{unique_gray}")
        findings["families"][family] = {
            "sheet": str(sheet.relative_to(ROOT)).replace("\\", "/"),
            "silhouette_hashes": hashes,
            "gray_means": grays,
        }

    # Cross-family: pickaxe vs axe at iron should differ.
    if "iron_pickaxe" in findings["items"] and "iron_axe" in findings["items"]:
        if (
            findings["items"]["iron_pickaxe"]["silhouette_hash"]
            == findings["items"]["iron_axe"]["silhouette_hash"]
        ):
            findings["failures"].append("same_silhouette:iron_pickaxe==iron_axe")

    findings["passed"] = len(findings["failures"]) == 0
    JSON_REPORT.write_text(json.dumps(findings, indent=2) + "\n", encoding="utf-8")

    lines = [
        "# Item visual QA (tools_visual)",
        "",
        "Date: 2026-11-13. Actual triangle/UV rasterization; native FP/TP acceptance is separate.",
        "",
        f"- Passed: **{findings['passed']}**",
        f"- Failures: {len(findings['failures'])}",
        f"- Warnings: {len(findings['warnings'])}",
        f"- Items audited: {len(findings['items'])}",
        "",
        "## Failures",
        "",
    ]
    if findings["failures"]:
        lines.extend(f"- `{f}`" for f in findings["failures"])
    else:
        lines.append("- none")
    lines += ["", "## Warnings", ""]
    if findings["warnings"]:
        lines.extend(f"- `{w}`" for w in findings["warnings"])
    else:
        lines.append("- none")
    lines += ["", "## Family sheets", ""]
    for family, info in findings["families"].items():
        lines.append(f"- `{family}`: [{info['sheet']}]({Path(info['sheet']).name})")
    lines += [
        "",
        "## Acceptance notes",
        "",
        "- Silhouette column must identify the family without color.",
        "- Gray column must keep wood/stone/metal tiers separable without hue.",
        "- Every published item must ship `textures/diffuse.png` (no factor-only).",
        "",
        "Per-item sheets: `docs/research/item-previews-v2/<id>_contact.png`.",
        "",
    ]
    REPORT.write_text("\n".join(lines), encoding="utf-8")
    return findings
