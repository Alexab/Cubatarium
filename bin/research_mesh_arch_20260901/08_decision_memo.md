# Decision memo — mesh architecture H0

**Status: GATE_PENDING** — код H0–M5 в ветке; autofly gates ожидают прогон с новой телеметрией.

## GO/NO-GO

| Gate | Result | Notes |
| --- | --- | --- |
| H0 baseline trio | PARTIAL | autofly + manual reports present; long pending |
| Parity documented | GO | gap quantified in `02_baseline_H0.md` |
| M0–M6 before H1 | NO-GO | harness parity required per plan §1.4 |

## Parity gap summary

Autofly `--replay-manual` is **not** representative of manual fly for holes (40% vs 95%) and witness diet (20% vs 61%). Mesh architecture phases must use H1-adjusted harness (`--replay-manual-fly-heavy`, `parity_vs_manual`, `MESH-parity-manual` gate) before claiming M1+ GO.

## Next actions

1. H1 SHIP: fly-heavy profile + teleport guard + parity gates
2. M0 waterfall telemetry live on cruise
3. M1 store diet + empty_fm_queue guard
4. M2 worker capture (TD-ARCH-046 close)

## Waivers

None.
