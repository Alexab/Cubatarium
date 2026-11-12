"""Prepare an isolated runtime candidate catalog, without publishing assets."""
from __future__ import annotations

import json
from pathlib import Path
import shutil

from creaturegen.hashutil import write_json
from creaturegen.pipeline import ROOT, MODELS, SKINS, build_species, build_skin, validate_staging, runtime_definition


def prepare(ids: list[str], destination: Path, skin_ids: list[str] | None = None) -> Path:
    destination = destination.resolve()
    scratch = (ROOT / "assetgen_output" / "creatures").resolve()
    if not destination.is_relative_to(scratch):
        raise ValueError("QA runtime bundle must remain under assetgen_output/creatures")
    models = destination / "models"
    shutil.copytree(MODELS, models / "creatures", dirs_exist_ok=True)
    shutil.copytree(SKINS, models / "skins", dirs_exist_ok=True)
    for species_id in ids:
        staged = build_species(species_id)
        errors = validate_staging(species_id, staged)
        if errors:
            raise ValueError(f"{species_id}: {errors}")
        target = models / "creatures" / species_id
        staged_pngs = {p.name for p in (staged/"textures").glob("*.png")}
        for p in (target/"textures").glob("*.png"):
            if p.name not in staged_pngs:
                p.unlink()
        shutil.copytree(staged, target, dirs_exist_ok=True)
        definition = runtime_definition(species_id, staged)
        write_json(target/"creature.json", definition)
    for skin_id in skin_ids or []:
        shutil.copytree(build_skin(skin_id), models / "skins" / skin_id, dirs_exist_ok=True)
    write_json(destination/"qa_catalog.json", {"species":ids,"skins":skin_ids or [],"models_root":str(models),
               "status":"candidate_only_not_published"})
    return models
