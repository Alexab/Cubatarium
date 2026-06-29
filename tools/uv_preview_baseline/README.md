# Creature preview baseline (Tier A)

Perceptual-hash reference images for `Cubatarium.exe --creature-preview-smoke`.

Populate after a local Release build:

```powershell
.\bin\Cubatarium.exe --creature-preview-smoke --tier-a
python tools/compare_creature_preview.py --update-baseline --tier-a
```

CI `windows-release-smoke` runs preview-smoke and compares against this directory.
Gate G12 is skipped until baselines are committed.
