#!/usr/bin/env python3
"""Download free texture packs into CubatariumTextureResearch."""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path
from urllib.parse import urljoin

try:
    import requests
except ImportError:
    print("Installing requests...", file=sys.stderr)
    subprocess.check_call([sys.executable, "-m", "pip", "install", "requests", "-q"])
    import requests

OUT_ROOT_DEFAULT = Path(r"E:/Work/Home/CubatariumTextureResearch")

PACKS = [
    {
        "id": "kenney_voxel_pack",
        "license": "CC0-1.0",
        "source_url": "https://kenney.nl/assets/voxel-pack",
        "download": {
            "type": "direct",
            "url": "https://opengameart.org/sites/default/files/voxel-pack-updated.zip",
        },
    },
    {
        "id": "kenney_pattern_pixel",
        "license": "CC0-1.0",
        "source_url": "https://kenney.nl/assets/pattern-pack-pixel",
        "download": {
            "type": "direct",
            "url": "https://opengameart.org/sites/default/files/kenney_pattern-pack-pixel.zip",
        },
    },
    {
        "id": "kenney_pattern_lines",
        "license": "CC0-1.0",
        "source_url": "https://kenney-assets.itch.io/pattern-pack-2",
        "download": {
            "type": "itch",
            "page_url": "https://kenney-assets.itch.io/pattern-pack-2",
        },
    },
    {
        "id": "minetest_default",
        "license": "CC-BY-SA-3.0",
        "source_url": "https://github.com/minetest-game/default",
        "download": {
            "type": "github_archive",
            "repo": "minetest-game/default",
            "paths": ["textures"],
        },
    },
    {
        "id": "seamless_pattern_pack",
        "license": "CC0-1.0",
        "source_url": "https://opengameart.org/content/seamless-pattern-pack",
        "download": {
            "type": "direct",
            "url": "https://opengameart.org/sites/default/files/seamless_pattern_pack.zip",
        },
    },
    {
        "id": "oga_16x16_blocks",
        "license": "CC0-1.0",
        "source_url": "https://opengameart.org/content/16x16-block-texture-set",
        "download": {
            "type": "direct_multi",
            "files": [
                {
                    "url": "https://opengameart.org/sites/default/files/blocks.zip",
                    "archive": True,
                },
                {
                    "url": "https://opengameart.org/sites/default/files/tilemap.png",
                    "archive": False,
                },
            ],
        },
    },
    {
        "id": "oga_mc_inspired",
        "license": "CC0-1.0",
        "source_url": "https://opengameart.org/content/cc0-minecraft-inspired-textures",
        "download": {
            "type": "oga_files",
            "base": "https://opengameart.org/sites/default/files/",
            "files": [
                "cc0.png",
                "terminus.png",
                "ruby_block.png",
                "rune.png",
                "shell.png",
                "something.png",
            ],
        },
    },
    {
        "id": "sbs_sandbox_terrain",
        "license": "CC0-1.0",
        "source_url": "https://screamingbrainstudios.itch.io/sbst-pack",
        "download": {
            "type": "itch",
            "page_url": "https://screamingbrainstudios.itch.io/sbst-pack",
            "upload_id": 0,
            "prefer_filename": "Texture Pack",
        },
    },
    {
        "id": "goncalo_pixel_patterns",
        "license": "CC0-1.0",
        "source_url": "https://goncalomcoliveira.itch.io/pixel-patterns",
        "download": {
            "type": "itch",
            "page_url": "https://goncalomcoliveira.itch.io/pixel-patterns",
        },
    },
    {
        "id": "vexed_block_land",
        "license": "CC0-1.0",
        "source_url": "https://v3x3d.itch.io/block-land",
        "download": {
            "type": "itch",
            "page_url": "https://v3x3d.itch.io/block-land",
        },
    },
]


def write_meta(pack_dir: Path, pack: dict) -> None:
    pack_dir.mkdir(parents=True, exist_ok=True)
    (pack_dir / "SOURCE_URL.txt").write_text(pack["source_url"] + "\n", encoding="utf-8")
    (pack_dir / "LICENSE.txt").write_text(
        f"License: {pack['license']}\nSource: {pack['source_url']}\n",
        encoding="utf-8",
    )


def download_direct(url: str, dest: Path, session: requests.Session) -> None:
    print(f"  GET {url}")
    with session.get(url, stream=True, timeout=120) as resp:
        resp.raise_for_status()
        with dest.open("wb") as f:
            for chunk in resp.iter_content(chunk_size=65536):
                if chunk:
                    f.write(chunk)


def find_unrar() -> str | None:
    for candidate in [
        shutil.which("unrar"),
        shutil.which("UnRAR"),
        r"C:\Program Files\WinRAR\UnRAR.exe",
        r"C:\Program Files\WinRAR\unrar.exe",
    ]:
        if candidate and Path(candidate).exists():
            return candidate
    return None


def extract_rar(archive: Path, dest: Path) -> None:
    dest.mkdir(parents=True, exist_ok=True)
    unrar = find_unrar()
    if unrar:
        subprocess.check_call([unrar, "x", "-y", str(archive), str(dest) + "\\"])
        return
    seven = find_7z()
    if seven:
        try:
            subprocess.check_call([seven, "x", str(archive), f"-o{dest}", "-y"])
            return
        except subprocess.CalledProcessError:
            pass
    try:
        import rarfile
    except ImportError:
        subprocess.check_call([sys.executable, "-m", "pip", "install", "rarfile", "-q"])
        import rarfile
    with rarfile.RarFile(archive) as rf:
        rf.extractall(dest)


def find_7z() -> str | None:
    for candidate in [
        shutil.which("7z"),
        shutil.which("7z.exe"),
        r"C:\Program Files\7-Zip\7z.exe",
        r"C:\Program Files (x86)\7-Zip\7z.exe",
        r"E:\Work\Home\CubatariumTextureResearch\_tools\7zip-full\7z.exe",
        r"C:\Program Files (x86)\Common Files\VolumeMeasureSDK\Runtime\Win64_x64\7z.exe",
    ]:
        if candidate and Path(candidate).exists():
            return candidate
    return None


def archive_kind(path: Path) -> str:
    head = path.read_bytes()[:8]
    if head[:2] == b"PK" or zipfile.is_zipfile(path):
        return "zip"
    if head[:4] == b"Rar!":
        return "rar"
    return "unknown"


def extract_archive(archive: Path, dest: Path) -> None:
    data = archive.read_bytes()[:4]
    if data[:2] == b"PK" or zipfile.is_zipfile(archive):
        with zipfile.ZipFile(archive) as zf:
            zf.extractall(dest)
        return
    if data == b"Rar!" or archive.suffix.lower() == ".rar":
        extract_rar(archive, dest)
        return
    raise RuntimeError(f"Unknown archive format: {archive}")


def itch_download(
    page_url: str,
    dest: Path,
    session: requests.Session,
    upload_id: int = 0,
    prefer_filename: str | None = None,
) -> None:
    print(f"  itch page {page_url}")
    page = session.get(page_url, timeout=60)
    page.raise_for_status()
    m = re.search(r'name="csrf_token"\s+value="([^"]+)"', page.text)
    if not m:
        raise RuntimeError("itch: csrf token not found")
    csrf = m.group(1)
    headers = {"x-csrf-token": csrf, "Referer": page_url}
    base = page_url.rstrip("/")

    if upload_id == 0:
        resp = session.post(base + "/download_url", data={"upload_id": 0}, headers=headers, timeout=60)
        resp.raise_for_status()
        landing_url = resp.json().get("url")
        if not landing_url:
            raise RuntimeError("itch: empty download url")
        landing = session.get(landing_url, timeout=60)
        landing.raise_for_status()
        names = re.findall(r'class="name">([^<]+)</span>', landing.text)
        ids = re.findall(r'data-upload_id="(\d+)"', landing.text)
        uploads = list(zip(ids, names)) if ids and names and len(ids) == len(names) else []
        if not uploads:
            uploads = [(uid, "") for uid in ids]
        if not uploads:
            raise RuntimeError("itch: upload id not found on download page")
        if prefer_filename:
            chosen = next(
                (int(uid) for uid, name in uploads if prefer_filename.lower() in name.lower()),
                None,
            )
            upload_id = chosen if chosen is not None else int(uploads[0][0])
        else:
            upload_id = int(uploads[0][0])
        print(f"  upload_id {upload_id}")

    file_resp = session.post(f"{base}/file/{upload_id}", headers=headers, timeout=60)
    file_resp.raise_for_status()
    payload = file_resp.json()
    file_url = payload.get("url") or payload.get("external_url")
    if not file_url:
        raise RuntimeError(f"itch: no file url in {payload}")
    print(f"  file {file_url[:80]}...")
    blob = session.get(file_url, stream=True, timeout=300)
    blob.raise_for_status()
    with dest.open("wb") as f:
        for chunk in blob.iter_content(chunk_size=65536):
            if chunk:
                f.write(chunk)
    magic = dest.read_bytes()[:4]
    if magic[:2] == b"<!":
        raise RuntimeError("itch: downloaded HTML instead of archive")


def github_archive(repo: str, paths: list[str], dest: Path, session: requests.Session) -> None:
    """Download GitHub repo archive zip and copy selected top-level paths."""
    archive_url = f"https://github.com/{repo}/archive/refs/heads/master.zip"
    print(f"  github archive {archive_url}")
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        archive = tmp_path / "repo.zip"
        download_direct(archive_url, archive, session)
        extract_root = tmp_path / "extract"
        extract_root.mkdir()
        with zipfile.ZipFile(archive) as zf:
            zf.extractall(extract_root)
        roots = list(extract_root.iterdir())
        if len(roots) != 1 or not roots[0].is_dir():
            raise RuntimeError("github archive: unexpected zip layout")
        repo_root = roots[0]
        for sub in paths:
            src = repo_root / sub
            if not src.exists():
                raise RuntimeError(f"github archive: missing path {sub}")
            target = dest / sub
            if target.exists():
                shutil.rmtree(target)
            shutil.copytree(src, target)


def git_sparse(repo: str, sparse_paths: list[str], dest: Path) -> None:
    """Clone sparse checkout into a temp dir, copy paths out (no .git in research)."""
    print(f"  git sparse {repo}")
    with tempfile.TemporaryDirectory() as tmp:
        clone_dir = Path(tmp) / "repo"
        subprocess.check_call(
            ["git", "clone", "--depth", "1", "--filter=blob:none", "--sparse", repo, str(clone_dir)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        subprocess.check_call(
            ["git", "-C", str(clone_dir), "sparse-checkout", "set", *sparse_paths],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        subprocess.check_call(
            ["git", "-C", str(clone_dir), "checkout"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        for sub in sparse_paths:
            src = clone_dir / sub
            if not src.exists():
                raise RuntimeError(f"git sparse: missing path {sub}")
            target = dest / sub
            if target.exists():
                shutil.rmtree(target)
            shutil.copytree(src, target)


def download_pack(pack: dict, out_root: Path, session: requests.Session) -> None:
    pack_id = pack["id"]
    pack_dir = out_root / pack_id
    write_meta(pack_dir, pack)
    dl = pack["download"]
    dtype = dl["type"]
    print(f"[{pack_id}] {dtype}")

    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        if dtype == "direct":
            archive = tmp_path / "pack.zip"
            download_direct(dl["url"], archive, session)
            extract_archive(archive, pack_dir)
        elif dtype == "direct_multi":
            for item in dl["files"]:
                url = item["url"]
                fname = Path(url).name
                target = tmp_path / fname
                download_direct(url, target, session)
                if item.get("archive"):
                    extract_archive(target, pack_dir)
                else:
                    shutil.copy2(target, pack_dir / fname)
        elif dtype == "oga_files":
            files_dir = pack_dir / "files"
            files_dir.mkdir(parents=True, exist_ok=True)
            for name in dl["files"]:
                url = urljoin(dl["base"], name)
                download_direct(url, files_dir / name, session)
        elif dtype == "itch":
            archive = tmp_path / "pack.bin"
            itch_download(
                dl["page_url"],
                archive,
                session,
                dl.get("upload_id", 0),
                dl.get("prefer_filename"),
            )
            kind = archive_kind(archive)
            if kind == "zip":
                archive = archive.rename(tmp_path / "pack.zip")
            elif kind == "rar":
                archive = archive.rename(tmp_path / "pack.rar")
            else:
                raise RuntimeError(f"itch: unknown archive format (magic {archive.read_bytes()[:4]!r})")
            extract_archive(archive, pack_dir)
        elif dtype == "github_archive":
            github_archive(dl["repo"], dl["paths"], pack_dir, session)
        elif dtype == "git_sparse":
            git_sparse(dl["repo"], dl["sparse_paths"], pack_dir)
        else:
            raise RuntimeError(f"Unknown download type: {dtype}")

    manifest = {
        "id": pack_id,
        "license": pack["license"],
        "source_url": pack["source_url"],
        "download_type": dtype,
    }
    (pack_dir / "pack_meta.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    png_count = sum(1 for _ in pack_dir.rglob("*.png"))
    print(f"  done: {png_count} PNG files")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out-root", type=Path, default=OUT_ROOT_DEFAULT)
    parser.add_argument(
        "--pack",
        action="append",
        dest="packs",
        metavar="ID",
        help="Download only these pack id(s); default: all",
    )
    args = parser.parse_args()
    out_root: Path = args.out_root
    out_root.mkdir(parents=True, exist_ok=True)
    (out_root / "analysis" / "raw").mkdir(parents=True, exist_ok=True)

    session = requests.Session()
    session.headers.update(
        {
            "User-Agent": "CubatariumTextureResearch/1.0 (local asset audit)",
        }
    )

    selected = set(args.packs) if args.packs else None
    packs_to_fetch = [p for p in PACKS if selected is None or p["id"] in selected]
    if selected:
        unknown = selected - {p["id"] for p in PACKS}
        if unknown:
            raise SystemExit(f"Unknown pack id(s): {', '.join(sorted(unknown))}")

    errors = []
    for pack in packs_to_fetch:
        try:
            download_pack(pack, out_root, session)
        except Exception as exc:
            print(f"  ERROR: {exc}", file=sys.stderr)
            errors.append((pack["id"], str(exc)))

    summary = {
        "ok": [p["id"] for p in packs_to_fetch if p["id"] not in {e[0] for e in errors}],
        "errors": errors,
    }
    (out_root / "download_summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    if errors:
        print(f"\n{len(errors)} pack(s) failed:", file=sys.stderr)
        for pid, msg in errors:
            print(f"  - {pid}: {msg}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
