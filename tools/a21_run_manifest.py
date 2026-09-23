#!/usr/bin/env python3
"""A21 P0: build a reproducible flight run manifest (binary ↔ world ↔ route)."""
from __future__ import annotations

import hashlib
import json
import os
import platform
import subprocess
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]


def _sha256_file(path: Path, *, max_bytes: int | None = None) -> str | None:
    if not path.is_file():
        return None
    h = hashlib.sha256()
    with path.open("rb") as f:
        if max_bytes is None:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        else:
            remaining = max_bytes
            while remaining > 0:
                chunk = f.read(min(1 << 20, remaining))
                if not chunk:
                    break
                h.update(chunk)
                remaining -= len(chunk)
    return h.hexdigest()


def _git(cmd: list[str]) -> str | None:
    try:
        out = subprocess.check_output(
            ["git", *cmd],
            cwd=ROOT,
            stderr=subprocess.DEVNULL,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        return out.strip() or None
    except (OSError, subprocess.CalledProcessError):
        return None


def _dirty_diff_hash() -> str | None:
    try:
        diff = subprocess.check_output(
            ["git", "diff", "HEAD"],
            cwd=ROOT,
            stderr=subprocess.DEVNULL,
        )
        status = subprocess.check_output(
            ["git", "status", "--porcelain"],
            cwd=ROOT,
            stderr=subprocess.DEVNULL,
        )
        payload = diff + b"\n" + status
        return hashlib.sha256(payload).hexdigest() if payload.strip() else "clean"
    except (OSError, subprocess.CalledProcessError):
        return None


def build_run_manifest(
    *,
    exe: Path | None = None,
    world: str | None = None,
    scenario: str | None = None,
    cold_warm: str | None = None,
    route_hash: str | None = None,
    extra: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """Collect identity fields for a flight / scorecard run."""
    git_sha = _git(["rev-parse", "HEAD"])
    branch = _git(["rev-parse", "--abbrev-ref", "HEAD"])
    dirty = _dirty_diff_hash()
    exe_path = Path(exe) if exe else None
    exe_hash = _sha256_file(exe_path) if exe_path else None

    world_hash = None
    if world:
        # Prefer a compact hash of world metadata / first MB of save if present.
        candidates = [
            ROOT / "bin" / "worlds" / world,
            ROOT / "worlds" / world,
            ROOT / "bin" / world,
        ]
        for c in candidates:
            if c.is_file():
                world_hash = _sha256_file(c, max_bytes=1 << 20)
                break
            if c.is_dir():
                meta = c / "world.json"
                if meta.is_file():
                    world_hash = _sha256_file(meta)
                    break

    manifest: dict[str, Any] = {
        "schema": "a21_run_manifest.v1",
        "git_sha": git_sha,
        "git_branch": branch,
        "dirty_diff_hash": dirty,
        "exe_path": str(exe_path) if exe_path else None,
        "exe_hash": exe_hash,
        "build_flags": {
            "config": os.environ.get("CUBA_BUILD_CONFIG", "Release"),
            "platform": platform.platform(),
            "python": sys.version.split()[0],
        },
        "backend": os.environ.get("CUBA_RENDER_BACKEND", "desktop-gl"),
        "gl_capabilities": os.environ.get("CUBA_GL_CAPS"),
        "gpu_driver": os.environ.get("CUBA_GPU_DRIVER"),
        "resolution": os.environ.get("CUBA_RESOLUTION"),
        "light_distance_settings": os.environ.get("CUBA_LIGHT_DISTANCE"),
        "world": world,
        "world_seed_or_hash": world_hash,
        "scenario": scenario,
        "route_hash": route_hash
        or (
            hashlib.sha256(f"{scenario}:{world}:{cold_warm}".encode()).hexdigest()
            if scenario
            else None
        ),
        "cold_warm_mode": cold_warm,
        "fog_on": os.environ.get("CUBA_FLIGHT_FOG_ON") == "1",
        "teleport_cruise": False,
        # A31 P0: completeness flags — empty optional env fields stay UNTESTED.
        "manifest_required_empty": [],
        "warm_protocol": None,
    }
    required_keys = (
        "git_sha",
        "dirty_diff_hash",
        "exe_hash",
        "world",
        "scenario",
        "cold_warm_mode",
    )
    empty: list[str] = []
    for k in required_keys:
        if not manifest.get(k):
            empty.append(k)
    for k in (
        "gl_capabilities",
        "gpu_driver",
        "resolution",
        "light_distance_settings",
        "world_seed_or_hash",
    ):
        if not manifest.get(k):
            empty.append(k)
    manifest["manifest_required_empty"] = empty
    if cold_warm == "warm":
        protocol = os.environ.get("CUBA_WARM_PROTOCOL", "warmup_sec")
        manifest["warm_protocol"] = protocol
    if extra:
        manifest.update(extra)
        # Recompute empty after extra merge for acceptance gates.
        empty2: list[str] = []
        for k in required_keys:
            if not manifest.get(k):
                empty2.append(k)
        for k in (
            "gl_capabilities",
            "gpu_driver",
            "resolution",
            "light_distance_settings",
            "world_seed_or_hash",
        ):
            if not manifest.get(k):
                empty2.append(k)
        manifest["manifest_required_empty"] = empty2
    return manifest


def manifest_acceptance_ok(manifest: dict[str, Any], *, for_acceptance: bool) -> dict[str, Any]:
    """A31: acceptance requires clean dirty hash and filled identity fields."""
    fails: list[str] = []
    empty = list(manifest.get("manifest_required_empty") or [])
    # Soft env fields are UNTESTED, not hard fail unless for_acceptance and listed.
    hard = {"git_sha", "exe_hash", "world", "scenario", "cold_warm_mode"}
    for k in empty:
        if k in hard:
            fails.append(f"manifest_missing:{k}")
    dirty = manifest.get("dirty_diff_hash")
    if for_acceptance and dirty and dirty != "clean":
        fails.append("dirty_diff_hash_not_clean")
    if manifest.get("cold_warm_mode") == "warm":
        if not manifest.get("warm_protocol"):
            fails.append("warm_protocol_unspecified")
    return {
        "manifest_acceptance_pass": len(fails) == 0,
        "manifest_acceptance_fails": fails,
        "manifest_untested_fields": [k for k in empty if k not in hard],
    }


def main() -> int:
    ap_world = None
    if len(sys.argv) > 1:
        ap_world = sys.argv[1]
    m = build_run_manifest(world=ap_world, scenario="cli", cold_warm="n/a")
    print(json.dumps(m, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
