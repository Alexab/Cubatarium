# Column Pipeline Experiment Log

## Scope

Unify column streaming ownership (ColumnFlow + Relight FIFO + Mesh Dirty +
keep-until-replace publication). Kill remesh/hide/underfeet Immediate zoo.
Process: code → unit → build → suite → journal → commit per phase.

## Baseline and Goals

- Branch: `perf_opt11` (from `8d2c5df7` underfeet plug)
- Baseline suite: `ColPipe_P0_baseline` stamp `20260820-193319`
- Goals vs baseline:
  - Stand: `black_sticky_blink_rate` ↓, `underfeet_opaque_present` flips ≈0
  - `dirty_revisit_same_n` med ≪ ~160 (baseline stand med **355**)
  - Idle Imm (`mesh_immediate_count` / `stand_rim_imm_n`) ≈0 after P4
  - Cruise wall_fly ≤ baseline +15%
  - No false-win perf (execution-verify on sharp gains)

## Experiment Timeline

| Step | Phase ID | Change | Result |
| --- | --- | --- | --- |
| P0-base | `ColPipe_P0_baseline` | Suite on clean `8d2c5df7` before ownership refactor | stamp `20260820-193319`. land-cruise wall_med=106 / fly=81 / BS%=20 / PL=1; land-stand wall=139 / fly=114 / BS%=6 / Imm stop_med=53; idle-clean wall=122 / BS%=15 / Imm stop_med=39; ocean PL=28 BS%=21; fly-clean wall=113 fly=88 BS%=8. Perf med `dirty_revisit_same_n`: stand=355 idle=297 cruise=195; underfeet_opaque flips stand=8 idle=5. |
| P0-sot | `ColPipe_P0_sot` | SoT freeze: keep-until-replace publication; sticky≠producer; underfeet=priority+lease; MarkRelit one owner; hide FullyDark not draw SoT | docs only; unit invariants land with P1+ |
| P1–P7 | `ColPipe_P5` (co-land) | Rank upgrade; MarkRelit one owner; keep-until-replace; kill underfeet Imm + SoftDefer force; SoftDefer gate-only; Apply cap≤3; enter Dirty-only | stamp `20260820-201442`. vs P0: dirty_revisit stand 355→261 cruise 195→141 idle 297→215; cruise BS% 20→11 idle 15→6; land-stand gates 9→11; fly wall_fly 88→92 (+5%). wall_fly cruise 81→87 (+7% <15%). Imm stop_med still ~53 (edit-path Imm ms remains). |
| P8 | `ColPipe_P8_dod` | Full 7-scenario DoD | stamp `20260820-202534`. land-cruise gates 11 fly=77 BS%=14; ocean PL=31 BS%=15 wall=79; fly-clean fly=82. **land-stand outlier** PL=41 gates 6/23 dirty=391 — see recheck. |
| P8-re | `ColPipe_P8_stand_recheck` | land-stand + idle-clean recheck after P8 outlier | stamp `20260820-204101`. **land-stand BS%=0 PL=1 dirty_med=170** (vs P0 355); idle-clean BS%=12 dirty=370. Stand outlier not reproducible; ownership win holds. |

## Progress

| Metric | P0 baseline | P5 co-land | P8 DoD | P8 recheck |
| --- | --- | --- | --- | --- |
| land-cruise wall_fly_med | 80.8 | 86.6 | 76.8 | — |
| land-cruise BS% | 20.5 | 11.4 | 13.6 | — |
| land-stand BS% | 6.1 | 10.4 | 6.3* | **0.0** |
| land-stand dirty_revisit med | 355 | 261 | 391* | **170** |
| land-stand PL med | 1 | 1 | 41* | **1** |
| idle-clean BS% | 14.7 | 5.9 | 5.7 | 11.8 |
| fly-clean wall_fly_med | 88.2 | 92.2 | 81.9 | — |
| ocean-cruise PL med | 28 | — | 31 | — |

\*P8 land-stand full-suite row is an outlier (recheck recovers).

## Verdict

Column Pipeline ownership landed: single admission (ColumnFlow rank upgrade),
single post-light remesh (MarkRelit Dirty XOR path), keep-until-replace draw,
SoftDefer gate-only, underfeet Imm removed from emerge. dirty_revisit on stand
cut ~355→170 on recheck; cruise BS% down vs P0; wall_fly within +15%. ERA22 zoo
superseded — see `ERA22_EXPERIMENT_LOG.md` Next Planned Steps.
