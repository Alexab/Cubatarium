"""Creature visual backend string helpers."""

from __future__ import annotations

BONE_SKELETON_BACKENDS = frozenset(
    {"bone_skeleton", "skeletal_geo", "bedrock_geo", "skeletal"}
)


def is_bone_skeleton_backend(backend: str | None) -> bool:
    return backend in BONE_SKELETON_BACKENDS
