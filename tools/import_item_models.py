#!/usr/bin/env python3
"""Import curated CC0 item / armor glTF props into models/items/.

Supported upstream packs (all CC0 1.0):
  - Kenney Survival Kit     https://www.kenney.nl/assets/survival-kit
  - KayKit RPG Tools Bits   https://kaylousberg.itch.io/rpg-tools-bits
  - KayKit Fantasy Weapons  https://kaylousberg.itch.io/fantasy-weapons-bits
  - Quaternius Fantasy Props / Ultimate RPG Items
                            https://quaternius.com/

Usage:
  python tools/import_item_models.py --list
  python tools/import_item_models.py --source-id kenney_survival_kit --pack-root third_party/asset_cache/kenney_survival_kit --retarget-content
"""

from __future__ import annotations

import argparse
import json
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODELS = ROOT / "models" / "items"
CONTENT = ROOT / "content" / "items"
MANIFEST = ROOT / "tools" / "item_model_manifest.json"

sys.path.insert(0, str(ROOT / "tools"))
from glb_to_gltf import glb_to_gltf  # noqa: E402
from obj_to_gltf import mesh_to_gltf  # noqa: E402

DEFAULT_MANIFEST = {
    "sources": [
        {
            "id": "kenney_survival_kit",
            "name": "Kenney Survival Kit",
            "license": "CC0-1.0",
            "url": "https://www.kenney.nl/assets/survival-kit",
        },
        {
            "id": "kaykit_rpg_tools",
            "name": "KayKit RPG Tools Bits",
            "license": "CC0-1.0",
            "url": "https://kaylousberg.itch.io/rpg-tools-bits",
        },
        {
            "id": "kaykit_fantasy_weapons",
            "name": "KayKit Fantasy Weapons Bits",
            "license": "CC0-1.0",
            "url": "https://kaylousberg.itch.io/fantasy-weapons-bits",
        },
        {
            "id": "quaternius_fantasy_props",
            "name": "Quaternius Fantasy Props MegaKit / Ultimate RPG Items",
            "license": "CC0-1.0",
            "url": "https://quaternius.com/",
        },
        {
            "id": "cubatarium_parts_v1",
            "name": "Cubatarium parts_v1 educational stand-in glTF",
            "license": "CC0-1.0",
            "url": "https://github.com/",
        },
    ],
    "items": {},
}


def ensure_manifest() -> dict:
    if not MANIFEST.exists():
        MANIFEST.write_text(json.dumps(DEFAULT_MANIFEST, indent=2) + "\n", encoding="utf-8")
        return json.loads(json.dumps(DEFAULT_MANIFEST))
    return json.loads(MANIFEST.read_text(encoding="utf-8"))


def source_url(manifest: dict, source_id: str) -> str:
    for s in manifest.get("sources", []):
        if s.get("id") == source_id:
            return s.get("url", "")
    return ""


def list_items(manifest: dict) -> None:
    print("Sources:")
    for s in manifest.get("sources", []):
        print(f"  - {s['id']}: {s['name']} ({s['license']}): {s['url']}")
    print("\nItem mapping:")
    for item_id, spec in sorted(manifest.get("items", {}).items()):
        status = spec.get("status", "")
        extra = f" status={status}" if status else ""
        path = spec.get("path") or spec.get("glob")
        print(
            f"  {item_id}: source={spec.get('source')} path/glob={path} "
            f"role={spec.get('role', '?')} class={spec.get('class', '?')}{extra}"
        )


def resolve_source_file(pack_root: Path, spec: dict) -> Path | None:
    if spec.get("path"):
        p = pack_root / spec["path"]
        return p if p.is_file() else None
    glob_pat = spec.get("glob", "")
    if not glob_pat:
        return None
    matches = sorted(pack_root.glob(glob_pat))
    if not matches:
        return None
    if len(matches) > 1:
        pick = spec.get("pick", None)
        if pick is None:
            print(
                f"WARN multiple matches for {glob_pat} ({len(matches)}); "
                f"set path or pick. Using first: {matches[0].name}"
            )
            return matches[0]
        idx = int(pick)
        if idx < 0 or idx >= len(matches):
            return None
        return matches[idx]
    return matches[0]


def retarget_content(item_id: str) -> None:
    path = CONTENT / f"{item_id}.json"
    if not path.is_file():
        print(f"RETARGET skip (no content def): {item_id}")
        return
    data = json.loads(path.read_text(encoding="utf-8"))
    data["model"] = f"models/items/{item_id}/model.gltf"
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    print(f"RETARGET {path}")


def import_from_pack(
    pack_root: Path,
    manifest: dict,
    source_id: str,
    dry_run: bool,
    retarget: bool,
    item_filter: str | None,
) -> tuple[int, int]:
    """Returns (copied, existing_skips)."""
    if not pack_root.is_dir():
        raise SystemExit(f"pack root not found: {pack_root}")
    copied = 0
    existing_skips = 0
    url = source_url(manifest, source_id)

    for item_id, spec in sorted(manifest.get("items", {}).items()):
        if spec.get("source") != source_id:
            continue
        if item_filter and item_id != item_filter:
            continue
        if spec.get("status") == "parts_only":
            print(f"SKIP {item_id}: parts_only (no pack mesh)")
            continue

        src = resolve_source_file(pack_root, spec)
        if not src:
            role = spec.get("role", "new")
            print(f"SKIP {item_id}: no match for {spec.get('path') or spec.get('glob')}")
            if role == "existing":
                existing_skips += 1
            continue

        dest_dir = MODELS / item_id
        dest = dest_dir / "model.gltf"
        print(f"{'DRY ' if dry_run else ''}COPY {src} -> {dest}")
        if not dry_run:
            dest_dir.mkdir(parents=True, exist_ok=True)
            if src.suffix.lower() == ".glb":
                glb_to_gltf(src, dest_dir, "model.gltf")
                # Keep original binary for reference
                shutil.copy2(src, dest_dir / "model.glb")
            elif src.suffix.lower() in {".obj", ".fbx"}:
                mesh_to_gltf(src, dest_dir, "model.gltf")
            else:
                shutil.copy2(src, dest)
                for sib in src.parent.iterdir():
                    if sib == src:
                        continue
                    if sib.suffix.lower() in {".bin", ".png", ".jpg", ".jpeg", ".webp"}:
                        shutil.copy2(sib, dest_dir / sib.name)
            # Kenney/KayKit shared atlas folder next to GLB files.
            tex_src = src.parent / "Textures"
            if tex_src.is_dir():
                tex_dest = dest_dir / "Textures"
                tex_dest.mkdir(parents=True, exist_ok=True)
                for tex_file in tex_src.iterdir():
                    if tex_file.is_file() and tex_file.suffix.lower() in {
                        ".png",
                        ".jpg",
                        ".jpeg",
                        ".webp",
                    }:
                        shutil.copy2(tex_file, tex_dest / tex_file.name)
            meta = {
                "id": item_id,
                "format": "gltf",
                "license": "CC0-1.0",
                "source": source_id,
                "upstream_file": str(src.relative_to(pack_root)).replace("\\", "/"),
                "url": url,
            }
            (dest_dir / "ATTRIBUTION.json").write_text(
                json.dumps(meta, indent=2) + "\n", encoding="utf-8"
            )
            (dest_dir / "LICENSE.txt").write_text(
                f"CC0-1.0\n{url}\n", encoding="utf-8"
            )
            if retarget:
                retarget_content(item_id)
        elif retarget:
            retarget_content(item_id)
        copied += 1
    return copied, existing_skips


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true", help="List sources and mappings")
    parser.add_argument("--pack-root", type=Path, help="Extracted CC0 pack directory")
    parser.add_argument(
        "--source-id",
        help="Required with --pack-root: only import items from this source",
    )
    parser.add_argument("--item-id", help="Import a single item id")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument(
        "--retarget-content",
        action="store_true",
        help="Set content/items/<id>.json model path to imported glTF",
    )
    args = parser.parse_args()
    manifest = ensure_manifest()
    if args.list or not args.pack_root:
        list_items(manifest)
        if not args.pack_root:
            print("\nPass --source-id <id> --pack-root <extracted_pack> to import.")
            return

    if not args.source_id:
        raise SystemExit("--source-id is required with --pack-root")

    copied, existing_skips = import_from_pack(
        args.pack_root,
        manifest,
        args.source_id,
        args.dry_run,
        args.retarget_content,
        args.item_id,
    )
    print(f"Done. Imported {copied} item model(s). existing_skips={existing_skips}")
    if existing_skips:
        sys.exit(1)


if __name__ == "__main__":
    main()
