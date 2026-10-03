# Flight perf research 20260829 — Index

**Cruise visual stability + throughput**

| Field | Value |
| --- | --- |
| Date | 2026-08-29 |
| HEAD (start) | `3970d44e` (enter-load fix landed) |
| Mode | telemetry + no-teleport autofly gates |
| Status | **IN PROGRESS** |

## SoT logs

| ID | Path | Role |
| --- | --- | --- |
| Manual short | `bin/logs/perf_20260829-081522_22660.jsonl` | первичный симптом (2 min) |
| Manual long | TBD `fz-manual-long` baseline | gate-of-record ≥270s |

## Autofly policy (DoD)

**Whitelist (no-teleport):** `fz-cold-enter`, `fz-manual-plateau`, `fz-manual-long`, `fz-ne-frontier-stand`, `land-cruise`, `fly-clean`

**Blacklist:** `fz-validate`, `fz-inring-cruise` (teleport)

## Phase gates

`FP-enter`, `FP0`…`FP5` in `tools/flight_sim_phase_gate.py`

## Artifact checklist

| File | Status |
| --- | --- |
| `00_index.md` | DONE |
| `01_ownership_map.md` | DONE |
| `02_baseline_081522.md` | DONE |
| `02_baseline_long.md` | PENDING (after fz-manual-long) |
| `03_telemetry_pack.md` | DONE |
| `04_hotspot_matrix.md` | DONE |
| `05_industry_scorecard.md` | DONE |
| `06_arch_options.md` | DONE |
| `07_roadmap.md` | DONE |
| `08_decision_memo.md` | IN PROGRESS |

## One-line verdict (081522)

Ghost Dirty closed (`skip_orphan=0`); primary bottleneck = witness retarget + FM consumer starvation (`schedule_ok` med 1) + ticketed VB plateau (med 73).
