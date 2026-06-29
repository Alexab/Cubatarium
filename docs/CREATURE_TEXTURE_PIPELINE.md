# Creature texture pipeline (rigid_voxels v3)

## Primary path (A): per-part box_uv bake

1. Luanti research atlas + optional `.b3d` → [`tools/bake_rigid_creature_textures.py`](../tools/bake_rigid_creature_textures.py) v3
2. One unfold PNG + `.uv.json` sidecar per visual part (unique stem when sizes collide)
3. `texture_layout: box_uv` for all non-human mobs
4. Automated gates G01–G13 via [`tools/run_creature_uv_wave.py`](../tools/run_creature_uv_wave.py)

```powershell
python tools/run_creature_uv_wave.py --species sheep --fail-fast
python tools/run_creature_uv_wave.py --wave W1 --skip-preview --commit-wave
python tools/validate_creature_uv.py --all --fail-on-threshold
```

## Fallback B: `luanti_mesh` backend (deferred)

Use if Tier A visual metrics regress after two v3 iterations:

- `visual.backend: luanti_mesh` in `creature.json`
- Load `.b3d` + atlas directly (Luanti-style mesh renderer)
- Rigid boxes retained for collision/movement only

## Fallback C: Blockbench single-atlas (deferred)

Use if v3 and B are too costly:

- Blockbench Bedrock entity export → one geometry PNG per species
- `visual.atlas` + per-part UV offsets in JSON

## Status tracking

- [`docs/CREATURE_UV_CHECKLIST.yaml`](CREATURE_UV_CHECKLIST.yaml) — per-species gate status
- [`tools/creature_uv_thresholds.yaml`](../tools/creature_uv_thresholds.yaml) — metric thresholds
- [`tools/uv_quality_baseline.yaml`](../tools/uv_quality_baseline.yaml) — frozen metrics for regression
