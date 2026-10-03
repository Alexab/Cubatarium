# Era31 Autofly Void-Debt Parity (2026-08-10)

## Root cause (H4 fixed in harness)

Autofly `fly_void_near_max=0` was **altitude-blind telemetry**, not missing ocean debt:

- `DarkFaceVoidNearN` = unlit faces within **24m** of camera
- Old ocean scenarios: `hold_space=True` + default `MinAltitudeAboveSea=28` → eye ≫24m above sea faces
- Stress was warm resume + idle≥12 → early void peak eaten before fly analyze

## Harness fix (`tools/flight_sim_run.py`)

- `--min-alt-above-sea` forwarded to exe (ocean default **10**)
- Ocean: `hold_space=False`
- Cruise coords → **−550 / 110** (manual 122032/153653 corridor)
- `ocean-cruise-stress`: cold **teleport**, idle=3, warmup_sec=8, sprint KEEP

## Results (post-fix)

| Run | void max | holes | frontier | wall fly | Gate |
|-----|----------|-------|----------|----------|------|
| stress **before** (`era31_ocean_stress2`) | **0** | 34% | 0.90 | 47 | STRESS NO-GO |
| stress **after** (`era31_void_parity_stress`) | **3578** | **67%** | 0.96 | 100 | **OCEAN_CRUISE_STRESS GO** |
| smoke after (`era31_void_parity_smoke`) | **1585** | 89% | 0.87 | 99 | OCEAN_CRUISE NO-GO (aspirational clean) |
| manual 153653 | 774 | 74% | 1.0 | 121 | OCEAN_MANUAL NO-GO |

## Semantics

- **OCEAN_CRUISE_STRESS** — parity DoD: must reproduce void≥400 + holes≥40% (now GO).
- **OCEAN_CRUISE** — aspirational heal targets; after altitude fix smoke is honest-dirty (NO-GO OK).
- **OCEAN_MANUAL** — still requires manual eye; autofly parity no longer false-clean on void.

## Commands

```powershell
python tools/flight_sim_run.py --scenario ocean-cruise-stress --phase-id era31_void_parity `
  --report bin/iter_reports/era31_void_parity_stress.json
python tools/flight_sim_phase_gate.py --phase-id OCEAN_CRUISE_STRESS `
  --report bin/iter_reports/era31_void_parity_stress.json
```
