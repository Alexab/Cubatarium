#!/usr/bin/env python3
"""Download free CC0 asset packs into third_party/asset_cache/."""

from __future__ import annotations

import json
import re
import ssl
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from http.cookiejar import CookieJar
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / "third_party" / "asset_cache"


def make_opener():
    cj = CookieJar()
    ctx = ssl.create_default_context()
    opener = urllib.request.build_opener(
        urllib.request.HTTPCookieProcessor(cj),
        urllib.request.HTTPSHandler(context=ctx),
    )
    opener.addheaders = [("User-Agent", "Mozilla/5.0 (Cubatarium asset import)")]
    return opener


def download_url(opener, url: Path, dest: Path) -> bool:
    dest.parent.mkdir(parents=True, exist_ok=True)
    print(f"GET {url}")
    try:
        with opener.open(url, timeout=120) as resp:
            data = resp.read()
    except urllib.error.HTTPError as e:
        print(f"  HTTP {e.code}: {e.reason}")
        return False
    except Exception as e:
        print(f"  FAIL: {e}")
        return False
    dest.write_bytes(data)
    print(f"  -> {dest} ({len(data)} bytes)")
    return len(data) > 1000


def extract_zip(zip_path: Path, dest_dir: Path) -> None:
    dest_dir.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path, "r") as zf:
        zf.extractall(dest_dir)
    print(f"  extracted -> {dest_dir}")


def itch_free_download(opener, slug: str, upload_index: int = 0) -> Path | None:
    """Download free tier from kaylousberg.itch.io/{slug}."""
    purchase_url = f"https://kaylousberg.itch.io/{slug}/purchase"
    html = opener.open(purchase_url, timeout=60).read().decode("utf-8", "replace")
    upload_ids = re.findall(r'data-upload_id="(\d+)"', html)
    if not upload_ids:
        print(f"  no upload_ids on {purchase_url}")
        return None
    uid = upload_ids[min(upload_index, len(upload_ids) - 1)]
    print(f"  upload_id={uid} (index {upload_index})")

    # Claim free purchase
    post_data = urllib.parse.urlencode(
        {"price": "0", "email": "", "csrf_token": ""}
    ).encode("utf-8")
    # Extract csrf from page if present
    csrf = re.search(r'name="csrf_token" value="([^"]+)"', html)
    if csrf:
        post_data = urllib.parse.urlencode(
            {"price": "0", "email": "", "csrf_token": csrf.group(1)}
        ).encode("utf-8")

    req = urllib.request.Request(
        purchase_url,
        data=post_data,
        method="POST",
        headers={"Content-Type": "application/x-www-form-urlencoded"},
    )
    try:
        opener.open(req, timeout=60).read()
    except urllib.error.HTTPError as e:
        # 302 redirect is OK
        if e.code not in (200, 302, 303):
            print(f"  purchase POST HTTP {e.code}")

    file_url = f"https://kaylousberg.itch.io/{slug}/file/{uid}"
    zip_dest = CACHE / f"{slug}.zip"
    if not download_url(opener, file_url, zip_dest):
        return None
    extract_dir = CACHE / slug.replace("-", "_")
    if slug == "rpg-tools-bits":
        extract_dir = CACHE / "kaykit_rpg_tools"
    elif slug == "fantasy-weapons-bits":
        extract_dir = CACHE / "kaykit_fantasy_weapons"
    extract_zip(zip_dest, extract_dir)
    return extract_dir


def quaternius_download(opener) -> Path | None:
    """Try multiple known Quaternius mirrors."""
    candidates = [
        "https://quaternius.com/assets/ultimate_rpg_items_pack_by_quaternius.zip",
        "https://quaternius.com/assets/ultimate_rpg_items_pack.zip",
        "https://quaternius.com/assets/UltimateRPGItemsPack.zip",
    ]
    zip_dest = CACHE / "quaternius_ultimate_rpg.zip"
    extract_dir = CACHE / "quaternius_fantasy_props"
    for url in candidates:
        if download_url(opener, url, zip_dest):
            extract_zip(zip_dest, extract_dir)
            return extract_dir
    # Poly.pizza bundle API (public)
    poly_url = "https://poly.pizza/api/bundle/download/h8mhlZ0dG8/gltf"
    if download_url(opener, poly_url, zip_dest):
        extract_zip(zip_dest, extract_dir)
        return extract_dir
    return None


def main() -> None:
    CACHE.mkdir(parents=True, exist_ok=True)
    opener = make_opener()
    results = {}

    print("=== KayKit RPG Tools ===")
    results["kaykit_rpg_tools"] = itch_free_download(opener, "rpg-tools-bits", 0)

    print("=== KayKit Fantasy Weapons ===")
    results["kaykit_fantasy_weapons"] = itch_free_download(
        opener, "fantasy-weapons-bits", 0
    )

    print("=== Quaternius Ultimate RPG ===")
    results["quaternius"] = quaternius_download(opener)

    print("\nSummary:")
    print(json.dumps({k: str(v) if v else None for k, v in results.items()}, indent=2))


if __name__ == "__main__":
    main()
