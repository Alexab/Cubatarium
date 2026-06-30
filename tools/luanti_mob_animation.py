#!/usr/bin/env python3
"""Parse mobs_redo mob Lua from CubatariumTextureResearch (animation, visual, mesh)."""

from __future__ import annotations

import re
from pathlib import Path

RESEARCH_DEFAULT = Path(r"E:/Work/Home/CubatariumTextureResearch")

# glTF clip name -> Luanti animation block keys (start, end)
CLIP_ALIASES: dict[str, tuple[str, str]] = {
    "idle": ("stand_start", "stand_end"),
    "walk": ("walk_start", "walk_end"),
    "run": ("run_start", "run_end"),
    "fly": ("fly_start", "fly_end"),
    "swim": ("fly_start", "fly_end"),  # Luanti aquatic mobs use fly_* for swim
    "punch": ("punch_start", "punch_end"),
    "die": ("die_start", "die_end"),
}

CLIP_SPEED_KEYS: dict[str, str] = {
    "idle": "stand_speed",
    "walk": "walk_speed",
    "run": "run_speed",
    "fly": "fly_speed",
    "swim": "fly_speed",
    "punch": "punch_speed",
    "die": "die_speed",
}

DEFAULT_CLIPS: tuple[str, ...] = ("idle", "walk", "run", "fly", "punch", "die")

# Relative to research root; searched in order for <species>.lua
SPECIES_LUA_SEARCH_DIRS: tuple[str, ...] = (
    "mobs_monster",
    "mobs_animal",
    "dmobs/mobs",
    "dmobs/dragons",
    "animalworld",
)


def _mob_mod_roots(research: Path) -> list[Path]:
    roots: list[Path] = []
    for rel in SPECIES_LUA_SEARCH_DIRS:
        p = research / rel.replace("/", "\\") if "\\" in str(research) else research / rel
        if p.is_dir():
            roots.append(p)
    return roots


def find_species_lua(species: str, research: Path | None = None) -> Path | None:
    """Locate mob Lua for a Cubatarium species id (filename stem match)."""
    root = research or RESEARCH_DEFAULT
    candidates: list[Path] = []
    for mod_dir in _mob_mod_roots(root):
        path = mod_dir / f"{species}.lua"
        if path.is_file():
            candidates.append(path)
    if not candidates:
        return None
    best: Path | None = None
    for path in candidates:
        text = path.read_text(encoding="utf-8", errors="replace")
        if _parse_animation_block(text):
            return path
        if best is None:
            best = path
    return best


def _mod_root_for_lua(lua_path: Path, research: Path) -> Path:
    rel = lua_path.relative_to(research)
    parts = rel.parts
    if len(parts) >= 2 and parts[0] == "dmobs":
        return research / "dmobs"
    return research / parts[0]


def _parse_animation_block(text: str) -> dict[str, int | float]:
    match = re.search(r"animation\s*=\s*\{", text)
    if not match:
        return {}
    start = match.end()
    depth = 1
    i = start
    while i < len(text) and depth > 0:
        ch = text[i]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
        i += 1
    block = text[start : i - 1]
    out: dict[str, int | float] = {}
    for key, value in re.findall(
        r"([a-zA-Z_][a-zA-Z0-9_]*)\s*=\s*(-?\d+(?:\.\d+)?)", block
    ):
        out[key] = float(value) if "." in value else int(value)
    return out


def parse_mob_properties(text: str) -> dict:
    """Extract Luanti Object Properties fields used by Cubatarium export."""
    props: dict = {}
    visual = re.search(r'visual\s*=\s*"([^"]+)"', text)
    if visual:
        props["visual"] = visual.group(1)
    mesh = re.search(r'mesh\s*=\s*"([^"]+)"', text)
    if mesh:
        props["mesh"] = mesh.group(1)
    glow = re.search(r"glow\s*=\s*(\d+)", text)
    if glow:
        props["glow"] = int(glow.group(1))
    scale = re.search(
        r"visual_scale\s*=\s*\{\s*x\s*=\s*([\d.]+)\s*,\s*y\s*=\s*([\d.]+)",
        text,
    )
    size = re.search(
        r"visual_size\s*=\s*\{\s*x\s*=\s*([\d.]+)\s*,\s*y\s*=\s*([\d.]+)",
        text,
    )
    if scale:
        props["visual_scale"] = (float(scale.group(1)), float(scale.group(2)))
    elif size:
        props["visual_scale"] = (float(size.group(1)), float(size.group(2)))
    box = re.search(
        r"collisionbox\s*=\s*\{([^}]+)\}",
        text,
    )
    if box:
        nums = [float(x) for x in re.findall(r"-?[\d.]+", box.group(1))]
        if len(nums) >= 6:
            props["collisionbox"] = nums[:6]
    props["animation"] = _parse_animation_block(text)
    return props


def load_mob_properties(species: str, research: Path | None = None) -> tuple[dict, Path | None]:
    lua_path = find_species_lua(species, research)
    if lua_path is None:
        return {}, None
    text = lua_path.read_text(encoding="utf-8", errors="replace")
    return parse_mob_properties(text), lua_path


def resolve_b3d_model_path(species: str, research: Path | None = None) -> str | None:
    """Return research-relative path like mobs_animal/models/mobs_cow.b3d if file exists."""
    root = research or RESEARCH_DEFAULT
    props, lua_path = load_mob_properties(species, root)
    mesh_name = props.get("mesh")
    if not mesh_name or not lua_path:
        return None
    mod_root = _mod_root_for_lua(lua_path, root)
    candidates = [
        mod_root / "models" / mesh_name,
        mod_root / mesh_name,
    ]
    for path in candidates:
        if path.is_file():
            return path.relative_to(root).as_posix()
    return None


def load_luanti_clips(
    species: str,
    research: Path | None = None,
    clips: tuple[str, ...] | None = None,
) -> tuple[dict[str, tuple[int, int]], float]:
    """Return clip frame ranges and fps from mob Lua."""
    clip_names = clips or DEFAULT_CLIPS
    props, _ = load_mob_properties(species, research)
    block = props.get("animation", {})
    if not block:
        return {}, 30.0
    fps = float(block.get("speed_normal", block.get("speed_run", 15)))
    if fps <= 0:
        fps = 15.0
    clip_frames: dict[str, tuple[int, int]] = {}
    seen_ranges: set[tuple[int, int]] = set()
    for clip_name in clip_names:
        alias = CLIP_ALIASES.get(clip_name)
        if not alias:
            continue
        start_key, end_key = alias
        if start_key not in block or end_key not in block:
            continue
        frame_start = int(block[start_key])
        frame_end = int(block[end_key])
        if frame_end <= frame_start:
            continue
        key = (frame_start, frame_end)
        if clip_name == "swim" and key in seen_ranges:
            continue
        seen_ranges.add(key)
        clip_frames[clip_name] = (frame_start, frame_end)
    return normalize_clip_frames(clip_frames), fps


def normalize_clip_frames(
    clip_frames: dict[str, tuple[int, int]],
) -> dict[str, tuple[int, int]]:
    """Drop zero-length clips; synthesize idle from walk/fly when Luanti omits stand_*."""
    out = {k: v for k, v in clip_frames.items() if v[1] > v[0]}
    if "idle" not in out:
        if "walk" in out:
            out["idle"] = out["walk"]
        elif "fly" in out:
            out["idle"] = out["fly"]
        elif "run" in out:
            out["idle"] = out["run"]
    return out


def build_gltf_state_map(
    habitat: str = "terrestrial",
    available_clips: set[str] | None = None,
) -> dict[str, str]:
    """Default locomotion state_map for creature.json given habitat and exported clips."""
    clips = available_clips or {"idle", "walk"}
    swim_clip = "swim" if "swim" in clips else ("fly" if "fly" in clips else "walk")
    run_clip = "run" if "run" in clips else "walk"
    fly_clip = "fly" if "fly" in clips else "idle"
    punch_clip = "punch" if "punch" in clips else "walk"

    state_map = {
        "Idle": "idle",
        "Walk": "walk",
        "Run": run_clip,
        "Jump": run_clip,
        "Fall": "idle",
        "Crouch": "idle",
        "Slither": "walk",
        "Coil": "idle",
        "Action": punch_clip,
        "Hover": fly_clip,
        "Glide": fly_clip,
    }
    if habitat in ("aquatic", "amphibious"):
        state_map["Swim"] = swim_clip
        state_map["Tread"] = "idle"
        state_map["Fly"] = fly_clip
    elif habitat == "aerial":
        state_map["Swim"] = swim_clip
        state_map["Tread"] = "idle"
        state_map["Fly"] = fly_clip
    else:
        state_map["Swim"] = swim_clip
        state_map["Tread"] = "idle"
        state_map["Fly"] = fly_clip if habitat == "aerial" else "idle"
    return state_map


def luanti_visual_scale_avg(props: dict) -> float:
    """Average Luanti visual_size / visual_scale factor (1.0 when unset)."""
    vs = props.get("visual_scale")
    if isinstance(vs, (list, tuple)) and len(vs) >= 2:
        return (float(vs[0]) + float(vs[1])) * 0.5
    if isinstance(vs, (list, tuple)) and vs:
        return float(vs[0])
    return 1.0


def apply_visual_scale_to_bounds(
    bounds: tuple[float, float, float], props: dict
) -> tuple[float, float, float]:
    scale = luanti_visual_scale_avg(props)
    return (
        max(bounds[0] * scale, 0.08),
        max(bounds[1] * scale, 0.08),
        max(bounds[2] * scale, 0.08),
    )


def luanti_bounds_rest(props: dict) -> tuple[float, float, float] | None:
    """Approximate creature bounds.rest [X,Y,Z] from collisionbox and visual_scale."""
    box = props.get("collisionbox")
    if not box or len(box) < 6:
        return None
    width_x = abs(float(box[3]) - float(box[0]))
    width_y = abs(float(box[4]) - float(box[1]))
    width_z = abs(float(box[5]) - float(box[2]))
    scale = luanti_visual_scale_avg(props)
    return (
        max(width_x * scale, 0.08),
        max(width_y * scale, 0.08),
        max(width_z * scale, 0.08),
    )


def build_creature_clip_defs(
    clip_frames: dict[str, tuple[int, int]],
    anim_block: dict[str, int | float],
) -> dict[str, dict]:
    """creature.json visual.animation.clips entries from Luanti frame ranges."""
    clips: dict[str, dict] = {}
    for name, (start, end) in clip_frames.items():
        speed_key = CLIP_SPEED_KEYS.get(name, f"{name}_speed")
        speed = float(anim_block.get(speed_key, anim_block.get("speed_normal", 15)))
        if speed <= 0:
            speed = 15.0
        loop_key = f"{name}_loop"
        loop = bool(anim_block.get(loop_key, 1))
        if name in ("die", "punch"):
            loop = bool(anim_block.get(loop_key, 0))
        clips[name] = {
            "start": start,
            "end": end,
            "loop": loop,
            "speed": speed / 15.0,
        }
    return clips
