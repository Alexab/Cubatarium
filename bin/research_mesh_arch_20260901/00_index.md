# Mesh Architecture Research — 20260901

| Field | Value |
| --- | --- |
| Date | 2026-09-01 |
| HEAD | `51f8dba7` (docs: fz-manual-long baseline gate-of-record) |
| Branch | `perf_opt16` |
| Status | H0 baseline captured · H1–M6 implementation in progress |

## SoT logs (H0 baseline trio)

| ID | Path / report | Role |
| --- | --- | --- |
| H0-autofly | `bin/suite_reports/i18_hotfix3_autofly.json` | `--replay-manual` no-teleport autofly |
| H0-manual | `bin/suite_reports/manual_20260901-124859_analyze.json` | ~9 min human fly (gate-of-record) |
| H0-long | pending `mesh_H0_manual_long.json` | `fz-manual-long` debt exposure |

Perf jsonl refs:
- Autofly: `bin/logs/perf_20260901-182316_13160.jsonl`
- Manual: `bin/logs/perf_20260901-124859_22296.jsonl`

## Autofly policy (mesh gates)

**Whitelist (no-teleport):** `--replay-manual`, `--replay-manual-fly-heavy`, `fz-manual-long`, `fz-manual-plateau`, `fz-cold-enter`

**Blacklist for M0–M6 gates:** `--teleport-cruise`, `fz-validate`, `fz-inring-cruise`

## Phase status

| Phase | Status | Notes |
| --- | --- | --- |
| H0 | GATE_PENDING | baseline trio + parity table in `02_baseline_H0.md` |
| H1 | IN_PROGRESS | replay-manual-fly-heavy, parity gates |
| M0 | IN_PROGRESS | waterfall telemetry + audit script |
| M1 | IN_PROGRESS | store diet, hard defer, FM queue guard |
| M2 | IN_PROGRESS | TD-ARCH-046 worker capture |
| M3 | IN_PROGRESS | GPU residency + pool batch |
| M4 | IN_PROGRESS | ColumnJobGraph ownership |
| M5 | IN_PROGRESS | commit-time skylight seed V3 |
| M6 | OPTIONAL | subchunk 16³ after M4 SHIP |

## One-line verdict

Main-thread `ChunkMeshSnapshot::Capture` + `empty_fm_queue` dominate; autofly holes 40% vs manual 95% — harness parity (H1) required before mesh phase gates are trustworthy.
