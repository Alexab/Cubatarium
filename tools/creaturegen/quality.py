"""Evidence gate, deliberately separate from structural validity and art judgement.

An export cannot review itself. Review evidence must refer to the exact staged
bundle, include real decoded images and an explicit reviewer decision. This
checks evidence integrity; the reviewer remains responsible for its content.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path

from PIL import Image

CRITERIA = ("recognizable_anatomy", "texture_quality", "rig_and_motion",
            "world_scale", "runtime", "ui_and_skins", "original_authorship")
ARTIFACTS = ("comparison", "motion", "runtime", "ui")


def bundle_hash(folder: Path) -> str:
    """Hash the complete payload, including filenames; exclude derived manifest."""
    digest = hashlib.sha256()
    manifest_path = folder / "manifest.json"
    if manifest_path.is_file():
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        context = {key:manifest.get(key) for key in
                   ("id", "backend", "model_scale", "runtime_definition_sha256", "source_inputs",
                    "toolchain","functional_contract_sha256")}
        if "base_geometry_sha256" in manifest:
            context["base_geometry_sha256"] = manifest["base_geometry_sha256"]
        digest.update(json.dumps(context,sort_keys=True,separators=(",", ":")).encode("utf-8"))
    for path in sorted(p for p in folder.rglob("*") if p.is_file() and p.name != "manifest.json"):
        digest.update(path.relative_to(folder).as_posix().encode("utf-8") + b"\0")
        digest.update(hashlib.sha256(path.read_bytes()).digest())
    return digest.hexdigest()


def published_payload_matches(staging: Path, published: Path) -> bool:
    for source in staging.rglob("*"):
        if not source.is_file() or source.name == "manifest.json":
            continue
        dest = published / source.relative_to(staging)
        if not dest.is_file() or hashlib.sha256(source.read_bytes()).digest() != hashlib.sha256(dest.read_bytes()).digest():
            return False
    manifest_path = staging / "manifest.json"
    if manifest_path.is_file():
        context = json.loads(manifest_path.read_text(encoding="utf-8"))
        wanted = context.get("runtime_definition_sha256")
        if wanted:
            definition = published / "creature.json"
            if not definition.is_file():
                return False
            actual = hashlib.sha256(json.dumps(json.loads(definition.read_text(encoding="utf-8")),
                sort_keys=True,separators=(",", ":"),ensure_ascii=False).encode("utf-8")).hexdigest()
            if actual != wanted:
                return False
    return True


def review_errors(species_id: str, folder: Path, review_path: Path | None) -> list[str]:
    if review_path is None or not review_path.is_file():
        return ["missing visual review; structural validity does not approve publication (--review)"]
    try:
        doc = json.loads(review_path.read_text(encoding="utf-8"))
        if doc.get("schema") != "creature-quality-review.v1":
            return ["unsupported review schema"]
        entry = doc.get("species", {}).get(species_id, {})
        errors = []
        if entry.get("decision") != "accepted" or not str(entry.get("reviewed_by", "")).strip():
            errors.append("missing explicit accepted decision and reviewer")
        if not entry.get("baseline_revision"):
            errors.append("missing comparison baseline revision")
        if entry.get("bundle_sha256") != bundle_hash(folder):
            errors.append("stale review: candidate bundle changed")
        for criterion in CRITERIA:
            if entry.get("criteria", {}).get(criterion) is not True:
                errors.append(f"unreviewed criterion: {criterion}")
        for kind in ARTIFACTS:
            artifact = entry.get("artifacts", {}).get(kind, {})
            raw_path = artifact.get("path", "")
            if not raw_path:
                errors.append(f"missing {kind} image evidence")
                continue
            path = (review_path.parent / raw_path).resolve()
            if not path.is_file():
                errors.append(f"missing {kind} file: {path}")
                continue
            if hashlib.sha256(path.read_bytes()).hexdigest() != artifact.get("sha256"):
                errors.append(f"changed {kind} evidence")
                continue
            try:
                with Image.open(path) as image:
                    image.load()
                    if min(image.size) < 32:
                        errors.append(f"unreadable scale of {kind} evidence")
            except (OSError, ValueError):
                errors.append(f"invalid {kind} image (markers are not renders)")
        return errors
    except (ValueError, TypeError, AttributeError) as exc:
        return [f"invalid review: {exc}"]


def require_review(species_id: str, folder: Path, review_path: Path | None) -> None:
    errors = review_errors(species_id, folder, review_path)
    if errors:
        raise RuntimeError(f"{species_id}: publication blocked: " + "; ".join(errors))
