# Comparative creature review

Baseline: `b35f00a75`; reviewed asset HEAD: `d35cc74e1`.
See the [audit](../../CREATURE_REDESIGN_COMMIT_AUDIT_2026-11-10_RU.md)
and [gallery](index.html). This folder contains actual decoded PNGs, not markers.

- `comparison_01..08.png`: all 56 species, baseline left / HEAD right,
  three rest views per version, equal world scale within each pair.
- `pair_<id>.png`: individual comparison cards.
- `motion_<id>.png`: six phases of actual glTF channels for six diagnostic species.
- `skins.png`: all five skins on the corresponding baseline/current geometry.
- `highlights.png`: four examples selected from the comparison cards.
- `metrics.json`: source-asset measurements captured **before** the audit changed
  the manifest's false accepted states; the original recorded value is 56 accepted.
- `checks.json`: offline validation logs and eight new regression tests.
- `runtime/`: fresh Release frames, run report, source/executable/capture context.

The offline renderer does not replace runtime acceptance. It omits lighting,
fog and C++ bone procedural pose profiles and has some Box-UV/mirror differences.
Both baseline and candidate contain defects; a baseline defect is not a target.
Motion sheet rows have a consistent scale across phases within each version;
the rest comparison cards additionally enforce equal scale across versions.

Regenerate from the repository root. If the ignored baseline folder is absent,
extract it safely from Git while rendering:

```powershell
python tools/research/review_creature_redesign.py --extract-baseline
```

If that folder already exists, omit `--extract-baseline`:

```powershell
python tools/research/review_creature_redesign.py
python tools/research/audit_creature_redesign_metrics.py
python tools/test_creaturegen_quality.py
```

Regenerated metrics reflect the **new** working-tree manifest state, not the
historical pre-audit accepted value. Git assets are extracted only into ignored
`assetgen_output/creatures/review_2026_11_10/baseline`; production generation
does not read that folder. Existing baseline data is never overwritten by extraction.

Runtime was compiled with the repository's mandatory Release command and run
through `tools/flight_sim_run.py` using `bin/Cubatarium.exe`. A new world was
created as `World_253`, not under the requested label. Captures are partial
gallery evidence, with occlusion and habitat limitations. Performance gates
were not evaluated; see `runtime/run_report.json` (`pass: null`).
