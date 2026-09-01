# Mesh Architecture Research — 20260901

| Field | Value |
| --- | --- |
| Date | 2026-09-01 |
| HEAD | `f62041bc` (+ CMake link fix uncommitted) |
| Branch | `perf_opt17` |
| Status | H0 trio captured · H1 harness landed · M0–M5 code landed · **GATE NO-GO** (holes/FM queue) |

## SoT logs (H0 baseline trio — post M0–M2)

| ID | Report | Perf jsonl | Role |
| --- | --- | --- | --- |
| H0-autofly | `bin/suite_reports/mesh_H0_replay_manual.json` | `perf_20260901-214457_12548.jsonl` | `--replay-manual` no-teleport |
| H0-fly-heavy | `bin/suite_reports/mesh_H1_fly_heavy.json` | `perf_20260901-215535_22828.jsonl` | `--replay-manual-fly-heavy` (H1) |
| H0-long | `bin/suite_reports/mesh_H0_manual_long.json` | `perf_20260901-215953_18844.jsonl` | `fz-manual-long` debt exposure |
| H0-manual | `bin/suite_reports/manual_20260901-124859_analyze.json` | `perf_20260901-124859_22296.jsonl` | ~9 min human fly (gate-of-record) |

## Autofly policy (mesh gates)

**Whitelist (no-teleport):** `--replay-manual`, `--replay-manual-fly-heavy`, `fz-manual-long`, `fz-manual-plateau`, `fz-cold-enter`

**Blacklist for M0–M6 gates:** `--teleport-cruise`, `fz-validate`, `fz-inring-cruise`

## Phase status

| Phase | Code | Gates (H0 replay) | Notes |
| --- | --- | --- | --- |
| H0 | DONE | GO baseline | trio + parity table in `02_baseline_H0.md` |
| H1 | DONE | parity_within_2x OK | fly-heavy profile; holes still 100% |
| M0 | DONE | GO waterfall | drain med ~100ms; snapshot main=0 (worker) |
| M1 | PARTIAL | NO-GO | empty_fm_queue blocker; holes 100% |
| M2 | PARTIAL | GO worker | main snapshot=0; M2c fallback remains |
| M3 | PARTIAL | GO pool | pool_unsync ≤50; GpuExtract scope not full |
| M4 | PARTIAL | NO-GO | guard on 1 path; holes 100% |
| M5 | MINIMAL | — | existing SeedDecision only |
| M6 | DEFERRED | — | `06_arch_options_M6.md` |

## One-line verdict

Worker capture removed main-thread snapshot (M2 GO), waterfall telemetry live (M0 GO), but **empty_fm_queue + holes_rate=100%** block M1/M4 SHIP — autofly still diverges from manual on hole class despite parity_within_2x on wall/stream.
