"""Small deterministic helpers. Copied in spirit from assetgen, not imported from it."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path


def stable_seed(*parts: object) -> int:
    text = "|".join(str(part) for part in parts)
    digest = hashlib.sha256(text.encode("utf-8")).digest()
    return int.from_bytes(digest[:8], "big")


def pixel_hash(image) -> str:
    rgba = image.convert("RGBA")
    return hashlib.sha256(rgba.tobytes()).hexdigest()


def file_hash(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path: Path, data: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
