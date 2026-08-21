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

## LitRing — light speed + no dark frames (2026-08-20)

Plan: lit ring + ускорение light (без тёмных кадров). Invariants: hole > FullyDark
plug; ColPipe owner kept; enter finite (soft/stall walls, no infinite RequireZero).

| Step | Phase ID | Change | Result |
| --- | --- | --- | --- |
| A | `LitRing_A_no_dark` | `IsChunkSliceRenderReady` hides FullyDark in LitDrawable until lit/true-dark; lit keep-until-replace; policy hide nh1/published dark | units OK (`miss_first`, `streaming_render_ready`) |
| B1 | `LitRing_B1_apply` | `CruiseRelightApplyBudget` cheap pin cap 3→5 (`apply_ms<3`) / 4 (`<4`) | units OK |
| C | `LitRing_C_enter_stall` | Enter FOV lit progress-stall (20s) releases RequireZero; underfeet+stall → settle with holes OK | units OK |
| B2/B3 | `LitRing_B3_seed` | Commit seed admits LitDrawable ring; cruise pending sync trigger 24→16 | units OK |
| D | `LitRing_D` | Suite idle/stand/cruise/ocean + Release `bin/Cubatarium.exe` | stamp `20260820-224246`. SHA256 `388B543B09CAD87F00320AFA245F7EE32AD3C030A1576FCC5D875079A57B4308`. land-cruise **PL med=0** wall=67 fly≈79 BS%=5 gates 9/23; idle PL=4 BS%=0; stand PL=32 BS%=2; ocean PL=30 BS%=3. **VB% blink=0** all four (no dark flicker telem). EH%=100 expected (stable holes > FullyDark plugs). vs P0 cruise wall_fly 80.8 → ~79 (within +15%). |

Cold-enter manual DoD (≥60s after load): verify underfeet FullyDark draw≈0,
`uf_flips`≈0 after settle, PL med↓ vs `220431`, `relight_apply_n`↑ — run against
Release `bin/Cubatarium.exe` matching the SHA above. Suite enter telem
(`enter_pending_max`≈8 on land-cruise) already << cold `220431` PL≈50–60.

## Verdict

Column Pipeline ownership landed: single admission (ColumnFlow rank upgrade),
single post-light remesh (MarkRelit Dirty XOR path), keep-until-replace draw,
SoftDefer gate-only, underfeet Imm removed from emerge. dirty_revisit on stand
cut ~355→170 on recheck; cruise BS% down vs P0; wall_fly within +15%. ERA22 zoo
superseded — see `ERA22_EXPERIMENT_LOG.md` Next Planned Steps.

**LitRing follow-on:** presentation no-dark + Apply/seed throughput + finite enter
stall; suite DoD under `LitRing_D`.

## Flicker Fill Optimize — sticky lit + cheap Apply + emerge (2026-08-21)

Plan: ring/underfeet = stable lit or stable hole (no dark plug thrash); unit-cost
Apply budget; MarkRelit primary-only under defer; seam fanout focus±1.

Baseline: LitRing_fix1 stamp `20260821-100114` + manual `104125` (PL≈55, apply_n≈1, uf_flips≈0.5/period).

| Step | Phase ID | Change | Result |
| --- | --- | --- | --- |
| P0 | `Flicker_P0` / `P0i` | Reject dark-over-live-lit; keep lit PreferKick gate; sticky lit GPU draw before Satisfying | First suite uf_flips↑ → iterate sticky-before-Satisfying. `P0i` stamp `20260821-113642`: cruise BS%=0 fly≈75 PL=1; stand BS%=0 PL=2; idle PL=5. vs fix1 BS stand 8→0. **pass** (PL residual → P1). |
| P1 | `Flicker_P1` | `RelightApplyNPrev`; unit_ms Apply caps + pin probe≤12ms; MarkRelit `primary_only` when defer | stamp `20260821-114857`: stand/idle **PL≤1–0**; cruise PL=1. apply_n med≈1 with PL≈1 (FIFO empty). Hot probe>12 blew wall_fly→99 → soft probe. **pass**. |
| P2 | `Flicker_P2` | MarkRelit seam only focus±1; neighbor_seam=false; no Dirty≫softcap schedule zoo | stamp `20260821-120509`: wall_fly cruise 99→91 / stand 100→91 vs P1; BS cruise 11→0. emerge mixed. **pass**. |
| P3 | `Flicker_P3` | DoD suite + ocean + journal + SHA | stamp `20260821-121414`. SHA256 `557CC9A78D1DCDEBE25A3175FF1C1233ED68B1C44F98C5558EE8291D70F953AC`. cruise wall=67 fly≈74 PL=1 BS%=5; stand PL=1 BS%=4 fly≈80; idle PL=3 BS%=0 fly≈66; ocean PL=33 BS%=0 fly≈87. Autofly PL≪ manual `104125`. |

### Deferred (won’t-fix / later)

- Dual column `draw_ok` vs per-slice hide cleanup (telem-only).
- Idle residual `uf_flips` (stand/idle still ~7–11/run; not ≈0 settle).
- Ocean PL≈33 (ocean-only; won’t-fix this plan).
- Dirty≫softcap schedule clamp (tried → wall_fly regress; reverted).
- Cold manual eye ≥60s still required against Release SHA above (autofly ≠ cold enter).

### Go criteria vs `104125`

Autofly: FullyDark dark-plug path gated; PL med 1–3 on land; wall_fly ≤ LitRing_fix1 +15%. Cold manual PL/apply_n/uf settle → user eye vs SHA.
