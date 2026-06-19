#!/usr/bin/env python3
"""Sample Luanti b3d rotation keys into JSON hints for procedural pose tuning."""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))

from b3d_read import B3DKeyframe, find_b3d_bone, iter_b3d_bones, load_b3d_pose

RESEARCH_DEFAULT = Path(r"E:/Work/Home/CubatariumTextureResearch")

# Luanti chicken.lua clip ranges (1-based frame indices in mob defs).
DEFAULT_CLIPS = {
    "stand": (1, 30),
    "peck": (31, 70),
    "walk": (71, 90),
    "run": (91, 110),
}

# Bones that map to Cubatarium rigid parts for chicken tuning.
CHICKEN_BONES = (
    "Spine_Head",
    "Head",
    "Right_Leg",
    "Left_Leg",
    "Right_Wing",
    "Left_Wing",
)

# Preferred euler axis per bone clip (avoids gimbal/wrap false positives).
CHICKEN_AXIS_HINTS: dict[tuple[str, str], str] = {
    ("Spine_Head", "peck"): "roll",
    ("Right_Leg", "walk"): "pitch",
    ("Left_Leg", "walk"): "pitch",
    ("Right_Wing", "walk"): "roll",
    ("Left_Wing", "walk"): "roll",
}


def quat_euler_xyz_deg(w: float, x: float, y: float, z: float) -> tuple[float, float, float]:
    sinr_cosp = 2.0 * (w * x + y * z)
    cosr_cosp = 1.0 - 2.0 * (x * x + y * y)
    roll = math.degrees(math.atan2(sinr_cosp, cosr_cosp))
    sinp = 2.0 * (w * y - z * x)
    pitch = math.degrees(math.asin(max(-1.0, min(1.0, sinp))))
    siny_cosp = 2.0 * (w * z + x * y)
    cosy_cosp = 1.0 - 2.0 * (y * y + z * z)
    yaw = math.degrees(math.atan2(siny_cosp, cosy_cosp))
    return roll, pitch, yaw


def band_keyframes(
    keyframes: list[B3DKeyframe], frame_start: int, frame_end: int
) -> list[B3DKeyframe]:
    return [kf for kf in keyframes if frame_start <= kf.frame <= frame_end]


def euler_band_stats(keyframes: list[B3DKeyframe]) -> dict:
    rolls: list[float] = []
    pitches: list[float] = []
    yaws: list[float] = []
    for kf in keyframes:
        if kf.rotation is None:
            continue
        w, x, y, z = kf.rotation
        roll, pitch, yaw = quat_euler_xyz_deg(w, x, y, z)
        rolls.append(roll)
        pitches.append(pitch)
        yaws.append(yaw)

    def delta_range(values: list[float]) -> float:
        if len(values) < 2:
            return 0.0
        base = values[0]
        deltas: list[float] = []
        for v in values:
            d = v - base
            while d > 180.0:
                d -= 360.0
            while d < -180.0:
                d += 360.0
            deltas.append(d)
        return max(deltas) - min(deltas)

    def axis_stats(values: list[float], label: str) -> dict | None:
        if not values:
            return None
        swing = delta_range(values)
        return {
            "axis": label,
            "min_deg": round(min(values), 3),
            "max_deg": round(max(values), 3),
            "range_deg": round(max(values) - min(values), 3),
            "swing_deg": round(swing, 3),
        }

    axes = [
        axis_stats(rolls, "roll"),
        axis_stats(pitches, "pitch"),
        axis_stats(yaws, "yaw"),
    ]
    axes = [a for a in axes if a is not None]
    dominant = max(axes, key=lambda a: a["swing_deg"]) if axes else None
    return {
        "frames": len(keyframes),
        "axes": axes,
        "dominant_axis": dominant,
    }


def sample_bone_clips(
    path: Path, clips: dict[str, tuple[int, int]], bone_names: tuple[str, ...]
) -> dict:
    roots = load_b3d_pose(path)
    out_bones: dict[str, dict] = {}
    for bone_name in bone_names:
        bone = find_b3d_bone(roots, bone_name)
        if bone is None or not bone.keyframes:
            continue
        clip_stats: dict[str, dict] = {}
        for clip_name, (start, end) in clips.items():
            band = band_keyframes(bone.keyframes, start, end)
            if band:
                clip_stats[clip_name] = euler_band_stats(band)
        out_bones[bone_name] = {
            "total_keys": len(bone.keyframes),
            "clips": clip_stats,
        }
    return {
        "bones": out_bones,
        "bone_count": sum(1 for _ in iter_b3d_bones(roots)),
        "keyed_bone_count": sum(1 for b in iter_b3d_bones(roots) if b.keyframes),
    }


def axis_swing(clip_stats: dict, axis_name: str) -> float | None:
    for axis in clip_stats.get("axes", []):
        if axis["axis"] == axis_name:
            return float(axis["swing_deg"])
    return None


def chicken_recommended_tuning(sample: dict) -> dict:
    bones = sample.get("bones", {})
    rec: dict[str, float] = {}

    spine_peck = bones.get("Spine_Head", {}).get("clips", {}).get("peck")
    if spine_peck:
        swing = axis_swing(spine_peck, CHICKEN_AXIS_HINTS[("Spine_Head", "peck")])
        if swing is not None:
            rec["peck_pitch_deg"] = swing

    leg_swings: list[float] = []
    for leg_name in ("Right_Leg", "Left_Leg"):
        walk = bones.get(leg_name, {}).get("clips", {}).get("walk")
        if not walk:
            continue
        axis = CHICKEN_AXIS_HINTS[(leg_name, "walk")]
        swing = axis_swing(walk, axis)
        if swing is not None:
            leg_swings.append(swing)
    if leg_swings:
        rec["leg_swing_deg"] = max(leg_swings)

    for wing_name in ("Right_Wing", "Left_Wing"):
        walk = bones.get(wing_name, {}).get("clips", {}).get("walk")
        if not walk:
            continue
        swing = axis_swing(walk, CHICKEN_AXIS_HINTS[(wing_name, "walk")])
        if swing is not None:
            rec["wing_idle_swing_deg"] = swing
            break

    return {k: round(v, 1) for k, v in rec.items()}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--research", type=Path, default=RESEARCH_DEFAULT)
    parser.add_argument(
        "--model",
        default="mobs_animal/models/mobs_chicken.b3d",
    )
    parser.add_argument("--walk-start", type=int, default=71)
    parser.add_argument("--walk-end", type=int, default=90)
    parser.add_argument("--peck-start", type=int, default=31)
    parser.add_argument("--peck-end", type=int, default=70)
    parser.add_argument(
        "--out",
        type=Path,
        default=TOOLS / "debug_uv_overlays" / "chicken_pose_samples.json",
    )
    args = parser.parse_args()
    path = args.research.resolve() / args.model
    if not path.is_file():
        raise SystemExit(f"missing {path}")

    clips = dict(DEFAULT_CLIPS)
    clips["walk"] = (args.walk_start, args.walk_end)
    clips["peck"] = (args.peck_start, args.peck_end)

    sample = sample_bone_clips(path, clips, CHICKEN_BONES)
    walk_keys = sum(
        b.get("clips", {}).get("walk", {}).get("frames", 0)
        for b in sample["bones"].values()
    )
    peck_keys = sum(
        b.get("clips", {}).get("peck", {}).get("frames", 0)
        for b in sample["bones"].values()
    )

    out = {
        "model": str(args.model),
        "clips": clips,
        "sample": sample,
        "recommended": chicken_recommended_tuning(sample),
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")
    print(
        f"wrote {args.out} keyed_bones={sample['keyed_bone_count']} "
        f"walk_keys={walk_keys} peck_keys={peck_keys} "
        f"recommended={out['recommended']}"
    )


if __name__ == "__main__":
    main()
