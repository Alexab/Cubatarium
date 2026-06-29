# Creature preview baseline (Tier A)

Perceptual-hash reference images for `Cubatarium.exe --creature-preview-smoke`.

Frozen baseline for Tier A (8 mobs × 4 yaw BMPs):

```powershell
.\bin\Cubatarium.exe --creature-preview-smoke --tier-a
python tools/compare_creature_preview.py --update-baseline --tier-a
python tools/compare_creature_preview.py --tier-a --fail-on-diff
```

CI `windows-release-smoke` runs preview-smoke and compares against this directory.
Gate **G12** is enforced for species with a baseline subdirectory here.
