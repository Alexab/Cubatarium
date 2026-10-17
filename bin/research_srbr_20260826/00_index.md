# SRBR 20260826 — Index

**Streaming + Render Bottleneck Research**

| Field | Value |
| --- | --- |
| Date | 2026-08-26 |
| HEAD | `1f3f767554c1b1fa5313279baf0f50136eb8173d` (`1f3f7675`, FZ27-P17) |
| Baseline eye | `9f502dd50d81a0e07858268eef59fdfb8bc0196f` (FZ27-P13) |
| Mode | worktree/diff + telemetry (no binary A/B of P13) |
| Language | Russian reports |
| Status | **COMPLETE** research · **SRBR-P0 code landed** (await autofly/manual) |

## SoT logs (verified present)

| ID | Path | Role |
| --- | --- | --- |
| P13 eye-best | `bin/logs/perf_20260825-170807_29680.jsonl` | визуально лучший референс |
| P16 residual | `bin/logs/perf_20260825-221516_28600.jsonl` | sticky miss cruise |
| P17 SoT bad | `bin/logs/perf_20260826-100413_20156.jsonl` | discard storm + VB sticky |
| SoftDefer KEEP | `bin/logs/perf_20260826-104722_26064.jsonl` | SoftDefer PASS после P17 |
| Current manual | `bin/logs/perf_20260826-112418_14164.jsonl` | long stand never-heal |

## Autofly policy

**DoD whitelist (no-teleport only):**
- `fz-manual-parity`, `fz-cold-enter`, `fz-manual-plateau`, `fz-manual-long`
- `fz-ne-frontier-stand`, `fz-frontier-stand-resume`

**Blacklist for verdicts (teleport smoke only):**
- `fz-validate`, `land-cruise`, teleport branch of `fz-inring-cruise`

## SoftDefer KEEP gates
floor/spike ≤8 · SiteB ≤5 · keep≥160 · capture≥3 · PL drop≈0

## Artifact checklist

| File | Status | Owner todo |
| --- | --- | --- |
| `00_index.md` | DONE | p0-setup |
| `01_ownership_map.md` | DONE | p1a-ownership-map |
| `02_baseline_delta.md` | DONE | p1b-baseline-diff |
| `03_telemetry_pack.md` | DONE | p1c-telemetry-pack |
| `04_hotspot_matrix.md` | DONE | p2a / p2b / p2c |
| `05_best_practice_scorecard.md` | DONE | p3a |
| `06_arch_options.md` | DONE | p3b |
| `07_roadmap.md` | DONE | p4-roadmap |
| `08_decision_memo.md` | DONE | p4-decision-memo |

## Research verdict (one line)

Ghost Dirty (`skip_orphan`≈54) + ticketed unlit VB≈107 starve heal on long stand; SoftDefer/discard green; next = Package P0 then P1 (`07_roadmap.md`).
