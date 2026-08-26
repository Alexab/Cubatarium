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

## Cold Apply Throughput — Enter Apply reuse + Capture≤Apply (2026-08-21)

Plan: accelerate lagging Apply (reuse Enter budget 64 at PL>30), Capture backpressure,
always primary_only when moving, narrow Dirty Y-band, FIFO no-dup push. No new JSON knobs.

Baseline: manual `123613` PL med≈63, fifo≈72, apply_n med≈1; LitRing_fix1 `20260821-100114`.

| Step | Phase ID | Change | Result |
| --- | --- | --- | --- |
| A1–A5 | `ColdApply_A1` | Enter Apply@PL>30; throttle Capture on Completed+expensive Apply; primary_only\\|moving; skip sea/lateral band; FIFO InFlight/remainder no dup push | stamp `20260821-131045`. cruise/stand/idle **PL med 2–3** (vs manual 63). wall_fly cruise≈119 (burst hitch; DoD recheck). |
| DoD | `ColdApply_DoD` | land+ocean suite + SHA + journal | stamp `20260821-131938`. SHA256 `F8D116F4ACF32ADE2CC8D492EA26FFD9F1B560AC1F5E32A52B6CDE8BC0D4899C`. cruise PL=1 fly≈95 BS%=7; stand PL=2 fly≈72 gates 12/23; ocean PL=25 (↓ vs Flicker 33); idle PL=40 outlier (Deferred). |

### Deferred

- Idle PL spike on DoD (40) — investigate Capture throttle vs idle Apply path.
- Cold manual eye ≥60s vs SHA (autofly ≠ cold enter; verify PL↓ / black holes↓).
- wall_fly burst on first A1 run — accept short hitch; keep Enter budget SoT.

### Go vs `123613`

Autofly land PL≪20. Capture≤Apply. MarkRelit cheaper on cruise. Cold manual still required for eye.

## ColdFix — rollback A2/A3 + queue-depth + Enter double-Drain (2026-08-21)

Manual `133345` after ColdApply: fill↓ (mesh_completed 4→2), VB_stalled 0→5, cap_partial ×4.5,
apply_n still 1, completed med=0. A2 absolute throttle + A3 always-primary_only = regressors.

| Step | Phase ID | Change | Result |
| --- | --- | --- | --- |
| P0 | (batch) | Remove A2 `bg_cap=0` / boost invert; `primary_only=defer` only; keep A5 FIFO no-dup + A4-on-defer | code |
| P1 | (batch) | `ShouldAdmitRelightCapture(completed+inflight<max_inflight)`; SoftDefer/miss floor=1 | units |
| P2 | (batch) | high-PL cruise: Enter budget + **double** `DrainAsyncRelightResults` (Enter SoT) | code |
| P3 | (batch) | underfeet keep live GPU opaque despite FullyDark while repair progress | units |
| P4 | skip | apply/emerge not dominating autofly after P0–P3 | skipped |
| DoD | `ColdFix_DoD` | suite land+ocean + journal + SHA | stamp `20260821-135653`. SHA256 `82955B3957D89661D2C5D8C5DECD821271443B621219ACBBF063C2F72C19F8B0`. cruise PL=3 fly≈105 BS%=0; stand PL=2 fly≈102 BS%=0; **idle PL=1** (was 40); **ocean PL=7** (was 25). |

### Deferred

- Cold manual eye ≥60s vs SHA above (compare fill/VB/PL to `123613` / `133345`).
- P4 cheap MarkRelit only if cold still shows apply_ms≈10 + emerge≈50 after P0–P3.

### Go vs `133345` / `123613`

Autofly: land PL≪20, idle PL fixed, ocean PL≪25, BS%=0. Cold fill ≥`123613` + VB_stalled≈0 → user eye.

## ColdSupply — DrainUpTo + DynamicCapture + Dirty focus (2026-08-21)

Manual `142000` after ColdFix: no eye win — PL≈73, apply_n≈1, fifo_drop 607, emerge≈53.
Root cause: `DrainCompleted` ignored budget (`DrainAll`); DynamicCapture locked by fifo_drop/batch drain.

| Step | Phase ID | Change | Result |
| --- | --- | --- | --- |
| S0–S2 | (batch) | `DrainUpTo(max_per_frame)`; DynamicCapture on unit_ms + high-PL ignores fifo_drop; primary_only no RAA latch; emerge MaxOutside=0 under dirty/PL | units OK |
| DoD | `ColdSupply_DoD` | suite land+ocean + journal + SHA | stamp `20260821-145911`. SHA256 `CFF020C1F6A226185E6F3409A5E0B13E03E22925AB929B99EDA099AF0CD37C0D`. cruise PL=1 fly≈81 BS%=5; stand PL=1 fly≈78; ocean PL=33; idle PL=38 (Deferred vs ColdFix idle=1). |

### Deferred

- Cold manual ≥60s vs SHA (target vs `142000`: PL≪73, apply_n≥2, fifo_drop≪607, emerge↓).
- Idle/ocean PL residual on autofly — watch after cold eye.

### Go vs `142000`

Autofly land PL≪20 / wall_fly ≤ ColdFix. Cold supply unlock is the eye gate.

## RateMatch — time-slice Apply + Capture≤Apply + LitDrawable keep-lit (2026-08-21)

Manual `190534` after ColdSupply: FPS↑ but unstable (sim 80→160); apply burst max12;
PL≈60; blink↑; holes forever. Capture=2 > Apply=1; Enter×64 double Drain hitch.

| Step | Phase ID | Change | Result |
| --- | --- | --- | --- |
| R0–R2 | (batch) | single Drain; high-PL floor=4 (not Enter64); MissReservedMs slice; DynamicCapture only if apply_n≥2; admit depth=apply_cap; keep-lit LitDrawable | units OK |
| R3 | skip | autofly PL/idle healthy — no remesh follow-on | skipped |
| DoD | `RateMatch_DoD` | suite land+ocean + journal + SHA | stamp `20260821-193630`. SHA256 `E471682EF0F13A8DA4A8629714986E42858AD016A6F8E5E7EF63C3716CE29561`. cruise PL=2 fly≈90 BS%=0; stand PL≈2 fly≈73; **idle PL=0**; ocean PL=25. |

### Deferred

- Cold manual ≥60s vs SHA (target vs `190534`: sim p90↓, apply max≤4, PL≪60, blink/holes↓).

### Go vs `190534`

Autofly land PL≪20, idle settled, wall_fly stable. Cold eye is the gate for blink/holes.

## CheapRemesh — coalesce Dirty/remesh + face-light Capture (2026-08-21)

Manual `195128` after RateMatch: emerge≈42, dirty≈170, Apply≈10, PL≈43, unlit_hidden≈29;
black/blink still present. Inflight leave-in-Dirty revisit; MarkRelit sea overmark;
full neighbor light memcpy; SoftDefer dual FirstMesh+Dirty; keep-lit required repair.

| Step | Phase ID | Change | Result |
| --- | --- | --- | --- |
| C0 | (batch) | Inflight schedule → Dirty.RemoveAt | units OK |
| C1 | (batch) | MarkRelit lit-cy-only; sea inflate iff !drawable; neighbor MarkMissingSlices | units OK |
| C2 | (batch) | column_center_only face-shell neighbor light | units OK |
| C3 | (batch) | PrimaryLightUnchanged skip MarkRelit Dirty | units OK |
| C4 | (batch) | SoftDefer FirstMesh XOR MarkDirty; sticky witness (no better_horiz hop) | units OK |
| C5 | (batch) | keep-lit LiveGpu without repair | units OK |
| C6 | (batch) | coalesce idle DropRemesh keep_h max | units OK |
| DoD | `CheapRemesh_DoD` | suite land+ocean + journal + SHA | stamp `20260821-212845`. SHA256 `4CAFA31024DD245715BDB011455357B3AC5F568B4E1BA8F420704D3B3CFAB26A`. cruise PL=1 fly≈78 wall≈60 dirty_med=108; stand PL=1 fly≈78; **idle PL=0**; ocean PL=27 gates 9/23 (was 7). vs RateMatch cruise wall↓ PL↓. |
| C0b | follow-up | second Dirty schedule Inflight leave→RemoveAt (remesh loop ~4433) | after cold `214657` still revisit≈98 |

### Cold manual `214657` (~138s) vs `195128`

| Metric | `195128` | `214657` | Note |
| --- | --- | --- | --- |
| wall med/p90 | 116 / 288 | 127 / **202** | med↑ p90↓ |
| emerge med | 50.5 | **46.6** | still >42 |
| dirty med | 172 | **142** | target ≪170 ✓ |
| dirty_revisit | 132 | **98** | ↓; C0b for remesh path |
| PL med/end | 43 / 45 | **37 / 31** | ≤40 ✓ |
| apply_ms med | 10.2 | 13.8 | slight↑ |
| unlit_hidden med | 29 | **28** | flat |
| vb med | 81 | 81 | flat |
| uf_flips / rate | 10 / 0.20 | **8 / 0.12** | slight↓ |
| nbr_light_n | 9 | 9 | count chunks; C2 is face memcpy |

### Deferred

- emerge≪42 / unlit_hidden≪29 / VB↓ still open vs eye targets.
- wall med / apply_ms regress on cold (stream-dominated).

### Cold manual `220205` (~70s, post-C0b rebuild) vs `214657` / `195128`

| Metric | `195128` | `214657` | `220205` |
| --- | --- | --- | --- |
| dirty med | 172 | 142 | **118** |
| dirty_revisit | 132 | 98 | **85** |
| PL med/end | 43/45 | 37/31 | **14**/47 |
| emerge med | 50.5 | 46.6 | **42.2** |
| wall med/p90 | 116/288 | 127/202 | 145/265 |
| apply_ms med | 10.2 | 13.8 | **20.2** |
| stream med | 33 | 52 | **74** |
| unlit_hidden | 29 | 28 | **14** (shorter run) |
| vb med | 81 | 81 | **121** |

C0b: revisit 98→85. Dirty/PL/emerge continue down; wall/stream/apply_ms and VB worse — not closed on eye.

## ColdWall — stream cut + Dirty leave-in (2026-08-21)

After CheapRemesh cold `220205`: wall↑ via stream≈74 (Apply nested ≈20); revisit residual;
VB Capture floor without PL; SoftDefer disk scan; fluid_map spikes.

| Step | Phase ID | Change | Result |
| --- | --- | --- | --- |
| S0 | (batch) | PendingGpu RemoveAt; no RAA latch when moving; remesh over-cap RemoveAt | units OK |
| S1 | (batch) | SoftDefer scan CD if Owned ticketed; stride 8; FirstMesh short-circuit | units OK |
| S2 | (batch) | VB bg floor only if PL\|\|miss; CountVisibleBlack narrow band moving | units OK |
| S3 | (batch) | Prefetch skip moving; underfeet inflate iff !drawable; face cache MarkRelit | units OK |
| S4 | (batch) | FluidMapShouldThrottleCruise wall>50 / pending>24 | units OK |
| DoD | `ColdWall_DoD` | suite land+ocean + journal + SHA | stamp `20260821-223622`. SHA256 `5AEA79A9CFD44F356DE5FEDF691474E679874A3BD68D74F6DDAD72AF23E5DB09`. cruise PL=1 wall≈69 fly≈102; stand PL≈0.5 wall≈79; **idle PL=0**; ocean PL=33; fly-clean PL=1 wall≈89. vs CheapRemesh cruise wall 60→69 / fly 78→102 (soft regress); idle PL held 0. |

### Deferred

- Cold manual ≥60s vs `220205` (target: wall≪145, stream≪74, revisit≪85, apply_ms≤14).

### Go vs `220205`

Autofly land PL≪20 / idle PL=0. Cold eye is the gate for stream/VB.

### Cold manual `093018` (~11 min / 338 spikes, ColdWall uncommitted) vs `091818` / `220205` / `195128`

Build: ColdWall S0–S4 local (HEAD `723ed751`). Route slow cruise: chunks 479→599, fill≈0.18/s.

| Metric | `195128` | `220205` | `091818` (~46s) | **`093018`** (~676s) |
| --- | --- | --- | --- | --- |
| wall med / p90 | 116 / 288 | 145 / 265 | 118 / 199 | **113 / 141** |
| stream med | 33 | 74 | 39 | **38** |
| emerge med | 50.5 | 42.2 | 30.5 | **58.2** |
| dirty med | 172 | 118 | 151 | **153** |
| revisit med | 132 | 85 | 97 | **106** |
| PL med / end | 43 / 45 | 14 / 47 | 36 / 38 | **45 / 43** |
| apply_ms med | 10.2 | 20.2 | 11.1 | **9.7** |
| vb med | 81 | 121 | 63 | **58** |
| unlit_hidden med | 29 | 14 | 31 | **43** |
| uf_flips / rate | 10 / 0.20 | 8 / 0.23 | 4 / 0.17 | 16 / **0.05** |

**Segments (`093018`):**

| Window | wall med | stream med | PL med | vb med | notes |
| --- | --- | --- | --- | --- | --- |
| first ~60s (enter) | 146 | 74 | 10 | 119 | stream≈220205; fluid spikes |
| 60–180s (ramp) | 118 | 28 | 40 | 54 | PL climbs, VB falls |
| 180s+ (steady cruise) | **110–113** | **38** | **45–46** | **58–60** | flat plateau |
| last ~60s | 108 | 39 | 45 | 60 | no cooldown |

PL buckets: 278/338 spikes ≥41 — **steady-state PL≈45, not transient**.

**Gate vs `220205` (steady cruise, 180s+):**

| Criterion | Status |
| --- | --- |
| wall ≪ 145 | ✓ med 110–113 |
| stream ≪ 74 | ✓ med 38 |
| apply_ms ≤ 14 | ✓ med 8–10 |
| revisit ≪ 85 | ✗ med 106–110 |
| PL | ✗ med 45 (vs 14) |
| VB | ✓ med 58 (vs 121) |
| unlit_hidden | ✗ med 43 (vs 14) |
| duration ≥60s | ✓ |

**Verdict:** ColdWall **closes stream/wall/apply/VB** on long cold manual. **PL/revisit/dirty/emerge/unlit_hidden** remain open vs `220205`; short `091818` underestimated PL (ramp ended early). Top spikes: enter-load + `fluid_map_cpu` (p90=0 in cruise, max≈221 at start). Next eye work: PL steady ≈45 + revisit≈106 + unlit_hidden≈43, not stream.

## ColdPL — PL drain + revisit cut (2026-08-22)

After ColdWall `1ede5165` / manual `093018`: stream/wall closed; **PL≈45**, **revisit≈106** open.

| Step | Phase ID | Change | Result |
| --- | --- | --- | --- |
| 1A | `ColdPL-1A` | `ShouldFinalizeRelightUnderPlPressure` — focus ring finalize when PL>24 | policy + units |
| 1B | `ColdPL-1B` | PL>30: PendingGpu/Inflight leave-in; defer side-effects off; underfeet band on primary_only; ClearPendingLight iff GPU/inflight-only Dirty | code |
| 2A | `ColdPL-2A` | `RemeshDeferredRing` — over-cap/outside remesh defer vs leave-in revisit | code |
| 2B | `ColdPL-2B` | Skip dirty sort when revisit>80% dirty | code |
| 3 | `ColdPL-3` | Hole-query memo on focus move; SoftDefer rim CD 12 when PL>40; VB single-pass cy scan | code |
| 4 | `ColdPL-F4` | Underfeet `IsChunkSliceRenderReady` keep GPU/inflight during pending; telem `UnderfeetOpaquePresentPredicted` | code |

### Deferred

- Cold manual ≥180s post-ColdPL vs `093018` (target: PL med <20, revisit <85, stream ≤45).
- Autofly guard: cruise PL≤5, wall_fly no +15% vs LitRing.

## FlickerZero — VB drain + enter wall (2026-08-22)

Baseline manual `151946` (ColdPL `00dffb3f`): PL med 32, revisit 133, enter VB/no_ticket 117/108, uf_flips 0.32 (telem), black_sticky 0.

| Step | ID | Change | Target |
| --- | --- | --- | --- |
| V0 | forensics | `bin/tmp_cold_pl_forensics.py` — VB/no_ticket timeline, vb_blink, opaque gap, segments | audit |
| V1 | ticket cap | `VisibleBlackNoTicketRepairCap` / `VoidCap` — lift stale_cap min(2) | enter no_ticket <30 @60s |
| V2 | VB budget | `FrameStreamingBudget` no_ticket floor at PL=0; drain bump; `ShouldFinalizeRelightUnderVbPressure` | PL med <15 steady |
| V3 | uf latch | `UnderfeetOpaquePresentLatched` post-draw; perf/stream read latched | uf_flips <0.05 |
| V4 | remesh | lit-ring FullyDark defer skip; PL leave-in carve-out; deferred ring max 64 | revisit <95 |
| T1 | fluid enter | `FluidMapShouldThrottleEnter` baseline 8 / burst 12 | enter wall p90 <250ms |
| T2 | enter burst | `TickEnterFovLitPass` NotePendingLight horiz≤4; Capture bg≥2 when no_ticket>8 | VB slope −5/frame |
| T3 | revisit | dirty sort skip 92%; requeue cap size/4 | revisit med <95 |

### Deferred

- Manual ≥180s post-FlickerZero vs `093018` / `151946`.
- Autofly: cruise PL≤5, VB blink=0.

## FlickerZero-2 — revisit bisect + PL damp + perf SoT (2026-08-22)

Baseline manual `164441` (FZ Phase 1, commit `33b3bebb`): enter no_ticket **7** OK; regressions PL enter **48**, revisit steady **157**, uf_flips **0.34**, fluid p90 **246**.

| Step | ID | Change | Target |
| --- | --- | --- | --- |
| R1 | revisit-bisect | `ShouldSkipDeferRemeshUnderVbHealPressure` gated defer/RemoveAt; cache pressure setters | revisit steady <95 |
| R2 | pl-damp-t2 | `TickEnterFovLitPass` NotePendingLight guards; terrain lit-ring seed; Capture bg≥2 enter | PL enter <25 |
| R3 | perf-sot | `UnderfeetOpaquePresentForPerf` in FramePerfMonitor; raw/predicted JSON | uf_flips <0.05 |
| R4 | vb-steady | `ShouldFinalizeRelightUnderVbSteadyPressure`; FrameStreamingBudget steady floor; drain tiers | VB steady <40 |
| R5 | fluid-tail | `FluidMapShouldThrottleEnter` VB>40; incremental cold seed cursor | fluid p90 <200 |
| R6 | v1-second-pass | ColumnFlowExecutor 2nd stale collect when idle no_ticket>20 | no_ticket peak <80 |
| R7 | dirty-sort-vb | skip dirty sort when `VisibleBlackNoTicketPressure_>0` | revisit enter ≤65 |
| R8 | validation | `tmp_flickerzero_compare.py` gates; forensics predicted flips | manual ≥180s |

### Gate table vs `164441`

| Gate | Target | `164441` |
| --- | --- | --- |
| enter no_ticket med | <30 | 7 |
| PL enter med | <25 | 48 |
| PL steady med | <15 | 30 |
| revisit steady med | <95 | 157 |
| uf_flips rate | <0.05 | 0.34 |
| enter wall p90 | <250 ms | 344 |
| enter fluid p90 | <200 ms | 246 |
| VB steady med | <40 | ~55 |

### Deferred (pre-flight)

- Manual ≥180s post-FZ2 vs `164441` / `151946`.
- Autofly guard: cruise PL≤5, VB blink=0, wall_fly ≤ LitRing_D +15%.

### Diagnostics

- `bin/tmp_flickerzero_compare.py` — segment metrics + gate PASS/FAIL.
- `bin/tmp_cold_pl_forensics.py` — default log `151946`; no_ticket_blink, opaque_gap, segment PL/VB; `underfeet_opaque_present_predicted` flip rate when present.
- `bin/tmp_fz2_gate_check.py` — extended gate table (no_ticket_peak, stream steady, revisit enter).

### Results — manual `173621` (commit `e3efb9d8`, ~262s)

Log: `bin/logs/perf_20260822-173621_33656.jsonl` (131 spikes). Protocol: cold load + manual fly ≥180s post-FZ2 build.

| Gate | Target | `173621` | `164441` | Δ | Status |
| --- | --- | --- | --- | --- | --- |
| enter no_ticket med | <30 | **3.0** | 7.0 | −4 | PASS |
| PL enter med | <25 | **51.0** | 48.0 | +3 | FAIL |
| PL steady med | <15 | **29.0** | 30.0 | −1 | FAIL |
| revisit steady med | <95 | **175.0** | 157.5 | +18 | FAIL (регресс) |
| revisit enter med | ≤65 | **63.0** | 61.0 | +2 | PASS |
| uf_flips rate | <0.05 | **0.076** | 0.340 | −78% | FAIL |
| enter wall p90 | <250 ms | **209** | 344 | −135 ms | PASS |
| enter fluid p90 | <200 ms | **45** | 246 | −201 ms | PASS |
| VB steady med | <40 | **62.0** | 54.5 | +8 | FAIL |
| unlit_hidden steady | <20 | **29.0** | 22.5 | +6 | FAIL |
| no_ticket peak | <80 | **114** | 97 | +17 | FAIL |
| stream steady med | ≤35 | **42.8** | 41.2 | +2 | FAIL |
| black_sticky | 0 | **0** | 0 | 0 | PASS |

**Segments (`173621`):**

| Segment | wall med | PL | VB | no_ticket | revisit | fluid p90 |
| --- | --- | --- | --- | --- | --- | --- |
| enter 0–60s | 129.7 | 51.0 | 81.0 | 3.0 | 63.0 | 45.4 |
| steady 120s+ | 113.3 | 29.0 | 62.0 | 0.0 | 175.0 | 0.0 |

**Forensics:** vb_blink rate **0.405**; no_ticket_blink **0.252**; uf_flips **10** (rate 0.076); all flips with `draw_ok=1`. first15 no_ticket `[111→114→95→86…]` slope **+9.3/frame** (target ≤−8). PL buckets: 48% frames PL>40.

**Track verdict (code `e3efb9d8`, metrics `173621`):** R5/R7/R8 PASS on metrics; R1/R2/R3/R4/R6 partial or FAIL. **5/13 gates PASS.** Wins: enter wall/fluid, uf_flips −78%, enter no_ticket. Open: PL, steady revisit (регресс vs 164441), VB/unlit steady, no_ticket peak.

### Bisect plan (failing gates)

See [`.cursor/plans/flickerzero_phase_2_bisect.plan.md`](../../.cursor/plans/flickerzero_phase_2_bisect.plan.md) — Phase 2.1 bisect order (B1–B8), kill-switches, per-gate DoD.

### Deferred (post-173621)

- Autofly guard post-FZ2: cruise PL≤5, VB blink=0, wall_fly ≤ LitRing_D +15%.
- Manual ≥180s post-FZ2.1 (commit after `e3efb9d8`) vs `173621` / `164441`.

### FlickerZero-2.1 — code shipped (bisect fixes)

Commit after `e3efb9d8`. Tracks B1–B7 from [bisect plan](../../.cursor/plans/flickerzero_phase_2_bisect.plan.md):

| Track | Change |
| --- | --- |
| B1 | `TickEnterFovLitPass` inflight guard post-Enqueue; terrain seed FullyDark-only |
| B2 | enter repair cap≤4; vb_radius clamp 2 on enter; R6 second pass when enter+no_ticket>10 |
| B3 | defer threshold 8→12; `RuntimeTuning.Fz2DeferGated` kill-switch |
| B4 | steady finalize vb/pl 35/12; bg floor 3 when VB>50+PL>20; drain 14 idle vb>50 |
| B5 | skip `HighPlCruiseApplyFloor` when VB>40 |
| B6 | `UnderfeetOpaquePresentForPerf` monotonic max(latched,predicted) |
| B7 | (secondary — follows B3/B4 drain) |

**Awaiting manual flight** for gate verification vs `173621`.

### Results — manual `183257` (commit `dfa9a637`, ~238s)

Log: `bin/logs/perf_20260822-183257_18792.jsonl` (119 spikes). Post-FZ2.1 bisect build.

| Gate | Target | `183257` | `173621` | Δ | Status |
| --- | --- | --- | --- | --- | --- |
| enter no_ticket med | <30 | **5.0** | 3.0 | +2 | PASS |
| PL enter med | <25 | **43.0** | 51.0 | −8 | FAIL |
| PL steady med | <15 | **30.0** | 29.0 | +1 | FAIL |
| revisit steady med | <95 | **169.0** | 175.0 | −6 | FAIL |
| revisit enter med | ≤65 | **81.0** | 63.0 | +18 | FAIL (регресс) |
| uf_flips rate | <0.05 | **0.067** | 0.076 | −12% | FAIL |
| enter wall p90 | <250 ms | **225** | 209 | +16 ms | PASS |
| enter fluid p90 | <200 ms | **62** | 45 | +17 ms | PASS |
| VB steady med | <40 | **62.0** | 62.0 | 0 | FAIL |
| unlit_hidden steady | <20 | **25.0** | 29.0 | −4 | FAIL |
| no_ticket peak | <80 | **98** | 114 | −16 | FAIL |
| stream steady med | ≤35 | **48.6** | 42.8 | +6 | FAIL |
| black_sticky | 0 | **0** | 0 | 0 | PASS |

**Segments (`183257`):**

| Segment | wall med | PL | VB | no_ticket | revisit | fluid p90 |
| --- | --- | --- | --- | --- | --- | --- |
| enter 0–60s | 128.1 | 43.0 | 80.0 | 5.0 | 81.0 | 61.8 |
| steady 120s+ | 110.5 | 30.0 | 62.0 | 0.0 | 169.0 | 0.0 |

**Forensics:** vb_blink **0.378**; no_ticket_blink **0.244**; uf_flips **8** (0.067); opaque_gap med **11** (was 39). first15 no_ticket slope **+2.3/frame** (was +9.3). PL buckets: 72% frames PL>25.

**FZ2.1 track verdict:** код B1–B7 shipped; метрики — частичный прогресс (PL enter −8, peak −16, uf −12%), но **0/8 DoD gates closed**. Регрессия: enter revisit 63→81 (trade-off B2b enter second pass?). Must-keep wall/fluid/no_ticket/black_sticky сохранены.

### FZ2.1-B1b — Note removed from TickEnterFovLitPass enqueue_col

`NotePendingLightBeforeMesh` убран из `enqueue_col` (`World.cpp`); PL только через ColumnFlow + terrain commit. **Awaiting manual ≥180s** vs `183257`.

### Deferred (post-183257)

- Autofly guard: cruise PL≤5, VB blink=0.
- Bisect B2b off on enter if revisit stays >65 after B1b flight.
- `Fz2DeferGated=false` flight for revisit steady root cause.

## FlickerZero-2.2 — PL bisect + revisit/VB drain (2026-08-22)

Baseline: manual `184927` (B1b `4a8ee2d4`). Target: close remaining FZ2 gates.

| Track | Change |
| --- | --- |
| C1a | `Fz2LitRingSeed` default **false** — lit-ring seed off |
| C1b | terrain commit Note **FullyDark-only** via `TryNotePendingLightBeforeMesh` |
| C1c / O1 | idempotent `TryNotePendingLightBeforeMesh` + `RelightNoteSkippedDupN` |
| C2a | enter second collect pass **removed** (idle-only second pass) |
| C2b | enter idle `VisibleBlackNoTicketRepairCap` **≤2** |
| C2c | enter peak second collect when `no_ticket>80` |
| C3b / O3 | VB focus >50 stable 3f → skip defer; vb_blink sort skip |
| C4a | drain tier `vb>45` → idle **16** |
| C4b | finalize steady vb/pl **30/10** |
| C4c | `HighPlCruiseApplyFloor` when vb≤**50** |
| C6b | `UnderfeetOpaquePresentPredictedHeld` 3-frame hold |
| O4 | dirty sort throttle 1/3 frames when revisit≥80% |
| O5 | second pass narrower ring (`vb_radius-1`) |
| infra | `fz-validate` scenario + `bin/tmp_fz2_step_validate.py` (P-OPT) |

**Validation:** `python bin/tmp_fz2_step_validate.py --step FZ22-shipped --skip-build`

### Results — autofly `193650` (FZ2.2 shipped build, ~287 spikes)

Log: `bin/logs/perf_20260822-193650_6352.jsonl`. Autofly `fz-validate`.

| Gate | Target | `193650` | `184927` | Status |
| --- | --- | --- | --- | --- |
| PL enter med | <25 | **5.5** | 41.5 | **PASS** |
| PL steady med | <15 | **1.0** | 45.0 | **PASS** |
| enter no_ticket med | <30 | **0.0** | 1.0 | PASS |
| VB steady med | <40 | **11.0** | 65.0 | PASS |
| unlit_h steady med | <20 | **1.0** | 44.0 | PASS |
| stream steady med | ≤35 | **19.7** | 42.7 | PASS |
| uf_flips rate | <0.05 | **0.035** | 0.021 | PASS |
| revisit steady med | <95 | **157.0** | 172.0 | FAIL (−15) |
| revisit enter med | ≤65 | **82.0** | 69.0 | FAIL |
| no_ticket peak | <80 | **102** | 116 | FAIL |
| enter wall p90 | <250 ms | **409** | 205 | FAIL (autofly teleport) |
| black_sticky | 0 | **0** | 0 | PASS |

**Verdict:** autofly закрыл **PL enter/steady**, **VB/unlit/stream** — основной debt снят. Open: revisit steady, enter wall p90 (autofly cold), no_ticket peak. **Manual ≥240s** — gate of record для M4.

### Results — manual `201207` (commit `584255f3`, ~150s)

Log: `bin/logs/perf_20260822-201207_20824.jsonl` (75 spikes). Gate of record for FZ2.2 — **not closed**.

| Gate | Target | `201207` | `184927` | autofly `193650` | Status |
| --- | --- | --- | --- | --- | --- |
| enter no_ticket med | <30 | **0.0** | 1.0 | 0.0 | PASS |
| PL enter med | <25 | **54.5** | 41.5 | 5.5 | FAIL (хуже B1b; teleport autofly ≠ manual) |
| PL steady med | <15 | **44.0** | 45.0 | 1.0 | FAIL |
| revisit steady med | <95 | **124.0** | 172.0 | 157.0 | FAIL (−48 vs B1b) |
| revisit enter med | ≤65 | **75.5** | 69.0 | 82.0 | FAIL |
| uf_flips rate | <0.05 | **0.000** | 0.021 | 0.035 | PASS |
| enter wall p90 | <250 ms | **194** | 205 | 409 | PASS |
| enter fluid p90 | <200 ms | **70** | 0 | 0 | PASS |
| VB steady med | <40 | **70.0** | 65.0 | 11.0 | FAIL |
| unlit_h steady med | <20 | **43.0** | 44.0 | 1.0 | FAIL |
| no_ticket peak | <80 | **103** | 116 | 102 | FAIL |
| stream steady med | ≤35 | **46.6** | 42.7 | 19.7 | FAIL |
| black_sticky | 0 | **0** | 0 | 0 | PASS |

**Forensics:** finalize_rate **0.96**; vb_blink **0.373**; no_ticket_blink **0.360**; PL buckets 41+ = **67/75**. first15 no_ticket slope **+9.9/frame**.

**Verdict:** FZ2.2 **not closed** (5/13 PASS). Teleport `fz-validate` invalid for PL/VB DoD. Revisit −48 vs B1b — sole durable win. Raw `NotePendingLightBeforeMesh` still feeds PL from WorldStreaming / DeferFar / Capture / ChunkEmerge / RecoverUnlit (see Phase 2.3 D1).

## FlickerZero-2.3 — PL parity + no-teleport autofly (2026-08-22)

Baseline manual `201207`. Gate of record: **`fz-manual-parity`** / **`fz-cold-enter`** (no teleport). Teleport `fz-validate` = smoke only.

| Track | Change |
| --- | --- |
| infra | `fz-manual-parity` (resume), `fz-cold-enter` (cold); step_validate default no-teleport |
| D1 | TryNote all RAW Note sites + `relight_note_skipped_dup_n` in perf |
| D3 | RepairCap enter **≤4** (bisect ship) |
| C3a | `fz2_defer_gated=false` diagnostic flight |
| O2 | Capture finalize epoch dedup (`RelightFinalizeDedupN`) |
| O3 | Dirty `ScheduledThisFrame_` dedup |
| D4 | enter peak second pass thresh 70, remain≤2 |
| C7/C8 | suite + manual ≥240s M4 |

### Shipped code (`FZ23` batch on `584255f3`+)

D1 R1–R8 TryNote; FramePerfMonitor `relight_note_skipped_dup_n` / `relight_finalize_dedup_n`; D3 RepairCap≤4; O2 finalize epoch; O3 schedule-frame set; D4 peak remain≤2; C3b VB>40/stable≥2; flight_sim/`tmp_fz2_step_validate` **`--config Release`** (bin/ exe).

**Infra note:** early autofly `210519`–`213255` ran against **stale** `bin/Cubatarium.exe` (Debug build not copied to bin/). **Invalid for DoD.** Gate of record below = Release ship.

### Results — Release ship `fz-manual-parity` `215535` (DoD)

Log: `bin/logs/perf_20260822-215535_30124.jsonl` (no teleport, resume, **Release**).

| Gate | Target | `215535` | `201207` | Status |
| --- | --- | --- | --- | --- |
| PL enter / steady | <25 / <15 | 39.5 / **1** | 54.5 / 44 | FAIL (−27%) / **PASS** |
| revisit steady / enter | <95 / ≤65 | 146 / 87 | 124 / 75.5 | FAIL / FAIL |
| no_ticket peak | <80 | 97 | 103 | FAIL |
| VB / unlit_h / stream | <40 / <20 / ≤35 | **12** / **1** / **27** | 70 / 43 / 47 | **PASS** |
| wall/fluid/uf/black_sticky | PASS | PASS | PASS | PASS |
| vb_blink | ≤0.25 | **0.227** | 0.373 | **PASS** |
| finalize_rate | ≤0.85 | 0.94 | 0.96 | slight ↓ |
| `relight_note_skipped_dup_n` | grows | max **741** | — | OK |
| `relight_finalize_dedup_n` | grows | max **200** | — | OK |

### Results — Release ship `fz-cold-enter` `220031`

Log: `bin/logs/perf_20260822-220031_17392.jsonl`.

| Gate | Target | `220031` | vs `201207` | Status |
| --- | --- | --- | --- | --- |
| PL enter | ≤32 (−≥40%) / DoD <25 | **22.5** | 54.5 (−59%) | **PASS** |
| PL / VB / stream steady | <15 / <40 / ≤35 | **0 / 13 / 29** | — | **PASS** |
| revisit steady | <95 | 128 | 124 | FAIL |
| no_ticket peak | <80 | 97 | 103 | FAIL |
| note_skipped / finalize_dedup | | max **894** / **99** | — | OK |

**Verdict (Release):** D1 cold PL enter closed; PL/VB/stream steady closed; vb_blink ≤0.25. Still open: revisit, peak, resume PL enter (39.5). C3a kept gated=true + C3b VB>40/stable2 shipped. **D2** = this Release rebaseline (≥230s wall phases); **C8** operator M4 ≥240s still recommended for visual stamp. Deferred guard: cruise PL≤5 on `fz-manual-parity` when revisit closed.

### Stale autofly (pre-Release bin) — do not use

`210519`, `211016`, `211505`, `212538`, `212123`, `213255` — stale exe. C3a decision (keep gated) still stands qualitatively; numbers superseded by `215535`/`220031`.

### C7 suite (Release)

| Scenario | Log / report | Role |
| --- | --- | --- |
| `fz-manual-parity` | `215535` | **DoD** |
| `fz-cold-enter` | `220031` | PL enter stress |
| `idle-clean` | `fz23_idle-clean-rel.json` | smoke |
| `land-cruise` | teleport smoke only | not DoD |

### D2 rebaseline ≥240s Release `220907`

Log: `bin/logs/perf_20260822-220907_30924.jsonl` (idle45+fly100+stop100).

| Gate | `220907` | `201207` | Notes |
| --- | --- | --- | --- |
| PL enter / steady | 39.5 / **1** | 54.5 / 44 | steady closed |
| revisit enter / steady | **55.5** / 132 | 75.5 / 124 | enter **PASS** ≤65 |
| peak / first15 slope | 95 / **−1.6** | 103 / +9.9 | slope flipped (D4) |
| VB / stream | **11** / 41 | 70 / 47 | stream soft open on long stop |

### C8 closeout (autofly; operator M4 still recommended)

| Gate | Target | Release DoD (`215535` / cold `220031`) | `201207` |
| --- | --- | --- | --- |
| PL enter / steady | <25 / <15 | 39.5 FAIL / **1 PASS**; cold **22.5 PASS** | 54.5 / 44 |
| revisit steady / enter | <95 / ≤65 | 146 FAIL / 87 FAIL; D2 enter **55.5 PASS** | 124 / 75.5 |
| no_ticket peak | <80 | 97 FAIL | 103 |
| VB / unlit_h | <40 / <20 | **PASS** | 70 / 43 |
| stream steady | ≤35 | **27 PASS** (parity) | 46.6 |
| wall/fluid/uf/black_sticky | PASS | PASS (parity) | PASS |

**Phase 2.3 code tracks complete.** Full M4 DoD not closed (revisit/peak/resume PL enter). Operator: manual ≥240s on Release `bin/Cubatarium.exe`, then `python bin/tmp_fz2_gate_check.py <perf>`.

---

## FlickerZero Phase 2.4 (2026-08-23)

### Manual short resume `093406` — plateau driver (not C8)

Log: `bin/logs/perf_20260823-093406_25440.jsonl` (~50s, manual resume World_164).

| Window | PL med | VB med | revisit med | no_ticket |
| --- | --- | --- | --- | --- |
| enter 0–30s | ~24–37 | — | — | peak 94 |
| plateau 16–50s (after nt→0) | **54–68** | **51–81** | **108→176** | **0** |
| «steady» (n=25, spikes 12–24) | 50* | 63* | 104* | 0 |

\*Short flight (&lt;120s wall): **steady gates are invalid** — second half is still plateau, not cruise drain. Do not compare manual steady to autofly steady PASS on `215535` (PL=1 at 120s+).

Telem: `relight_note_skipped_dup_n`→237; `relight_finalize_dedup_n`→8; frequent `relight_capture_finalize=1` with `relight_apply_n=0`.

**Verdict:** Resume PL plateau after `no_ticket=0` is Phase 2.4 primary bug. `fz-cold-enter` (PL enter 22.5) is **not** a proxy for resume plateau. Autofly parity audit: segment gates + `fz-manual-plateau` (~105s) required before P0 code.

### Autofly parity audit (Release)

| Log | Role | Dur | PL enter | PL mid 60–120s | PL steady 120s+ |
| --- | --- | --- | --- | --- | --- |
| `093406` | manual short | ~50s | 37 | 50–68 (16–50s) | n/a |
| `215535` | `fz-manual-parity` | ~230s | 39.5 | ~38 | **1** |
| `220031` | `fz-cold-enter` | ~230s | 22.5 | — | 0 |

**False read:** long parity steady PASS masks mid-phase plateau (PL≈38–67 after nt=0). Primary DoD after infra = **`fz-manual-plateau`**; parity = steady regression only.

### Phase 2.4 tracks (in order)

0. Infra autofly — segment gates, `fz-manual-plateau`, `fz-manual-long`, parity compare vs `093406`
1. P0-A — `ShouldSuppressPendingLightNote` in `TryNotePendingLightBeforeMesh`
2. P0-B — plateau apply/capture boost when nt=0
3. P0-C — MarkDirty inflight+revision dedup; C3b VB&gt;30 / nt_thresh 8
4. P1 — finalize epoch window 2→4 + band carve-out; enter peak thresh 60 remain≤1
5. C7/C8 — suite Release + manual ≥240s gate table

**Hard nos unchanged:** LitRing unlit preview, Imm primary, DrainAll, fog hide, dark plug. **DoD exe:** Release only → `bin/Cubatarium.exe`.

### FZ2.4 closeout — Release autofly (post-implementation)

Log: `bin/logs/perf_20260823-112612_31228.jsonl` (`fz-manual-plateau`, run extended by fly_stop 420s cap — infra fix: plateau uses `seconds+120` timeout only).

| Gate | Target | FZ2.4 plateau | vs `215535` | vs manual `093406` |
| --- | --- | --- | --- | --- |
| PL enter | <25 | 34.5 FAIL | 39.5 | 37.0 |
| **PL mid 60–120s** | **<30** | **30.0** (border) | 38.0 (−21%) | n/a (short) |
| PL steady 120s+ | <15 | 3.5 PASS | 1.0 | n/a |
| revisit steady | <95 | 187 FAIL | 146 | — |
| no_ticket peak | <80 | 116 FAIL | 97 | 94 |
| note_suppressed_plateau | >0 | **305** | — | — |
| apply_plateau_boost | >0 | **79** | — | — |
| finalize_apply_ratio | ↑ | **0.96** | 0.87 | — |

**Verdict:** P0-A/B telem active; PL mid improved 38→30 (plateau gate borderline). Revisit/peak/VB steady still open. Operator C8: manual ≥240s on Release recommended.

### C7 suite matrix (FZ2.4)

| Scenario | Role | Duration |
| --- | --- | --- |
| `fz-manual-plateau` | **Primary DoD** — resume plateau window | ~105s |
| `fz-manual-parity` | Steady regression | ~230s |
| `fz-manual-long` | C8 proxy drain | ≥270s |
| `fz-cold-enter` | Cold enter stress only | ~230s |
| `idle-clean` | smoke | — |
| `land-cruise` | teleport smoke | not DoD |

### C8 manual gate table (≥240s)

| Gate | Target | `215535` | `093406` short |
| --- | --- | --- | --- |
| PL enter / steady | <25 / <15 | 39.5 / 1 | 37 / 50* |
| revisit steady / enter | <95 / ≤65 | 146 / 87 | 104 / 95 |
| no_ticket peak | <80 | 97 | 94 |
| VB / unlit_h / stream | <40 / <20 / ≤35 | PASS | FAIL* |
| wall/fluid/uf/sticky | PASS | PASS | PASS |

\*Short manual ≠ steady acceptance; compare enter + plateau mid only.

---

## FZ2.5 — Perf-first ticketed VB consumption

**Baseline:** `perf_20260823-114401_15212.jsonl` (~586s manual post-FZ2.4)

### Perf-0 bottleneck table (114401)

| Hypothesis | Signal | Value | Verdict |
| --- | --- | --- | --- |
| B1 Apply/time misaligned | apply_util steady | **0.05** | CONFIRMED — count budget 20, med apply_n=1 |
| B1 slice early-out | unit_apply_ms steady | **19.1** | CONFIRMED — 1 apply @~19ms fills 8ms slice |
| B2 Install starved | mark_relit_schedule steady | **0** | CONFIRMED |
| B4 mesh_schedule tax | stream steady | **63.5** | CONFIRMED — dominates p90 |
| B5 All ticketed debt | vb_ticketed / no_ticket | **81 / 0** | CONFIRMED |

**Bottleneck rank (steady):** mesh_schedule (69ms p90) > capture (24ms) > apply (22ms)

Script: `python bin/tmp_fz25_bottleneck.py bin/logs/perf_20260823-114401_15212.jsonl`

### Tracks shipped

| Track | Change |
| --- | --- |
| Perf-1 | `ShouldConsumeTicketedVbDebt`, `EarnedRelightApplyCap`, consume `primary_only`, adaptive multi-drain |
| P0-B | `ShouldForceMarkRelitForTicketedStale` — bypass enter quiesce/terminal for stalled tickets |
| P0-C | Finalize dedup carve-out + Capture depth ∝ earned Apply cap |
| Perf-2 | `ShouldPrioritizeMeshScheduleForTicketedConsume` — idle mesh_schedule floor 12 |
| Perf-3 | VB SoT cache/frame, stable-band cy tighten, ticketed early-out scan |
| P1 fallback | vb_thresh 25, idle bg_floor 4 (114401 VB=81 — perf insufficient alone) |

### P1-B enter peak audit

- FZ2.4 enter collect ring clamp `vb_radius ≤ 2` active (`ColumnFlowExecutor.cpp:387`)
- `VisibleBlackNoTicketRepairCap` enter idle cap=4 shipped
- Peak 110 on 114401 likely from first-pass collect + repair enqueue burst, not cap-only
- No additional radius tighten (would starve enter heal)

### FZ2.5 close gates (target vs 114401)

| Gate | Target | 114401 | Track |
| --- | --- | --- | --- |
| apply_util idle | ≥0.15 | 0.05 | Perf-1 |
| relight_apply_n med | ≥2 | 1 | Perf-1 |
| VB steady | <40 | 81 | P0+Perf+P1 |
| stream steady | ≤35 | 63.5 | Perf-2/3 |
| revisit steady | <95 | 116 | Perf-2 |
| stalled tail max | <10 | 17 peak | P0-B |
| mark_relit steady | >0 | 0 | P0-B |

### Industry checklist (C8)

- [x] Dual budget aligned (EarnedRelightApplyCap + consume slice)
- [x] Apply/Install separated (primary_only on consume)
- [x] No duplicate O(n) VB scan (SoT cache + ColumnFlow reads cache)
- [x] Stalled ticketed → control-plane MarkRelit force
- [x] mesh_schedule anti-starvation under ticketed consume
- [ ] wall p90 enter within +10% of 114401 (validate on next manual)
- [ ] VB steady <40 visual DoD (validate ≥240s manual)

**Validate:** `python bin/tmp_fz2_step_validate.py --step FZ25-C8 --build --scenario fz-manual-plateau --baseline-manual bin/logs/perf_20260823-114401_15212.jsonl`

---

## FZ2.6 — Budget reality + validation loop (2026-08-23)

**Baseline:** `perf_20260823-125933_8084.jsonl` (post-FZ2.5)  
**Regression guard:** `perf_20260823-114401_15212.jsonl` (FZ2.4)

### BRM summary

| BRM | Finding |
| --- | --- |
| BRM-1 | Count budget (20) not binding; time budget (8ms) binds at unit~19ms |
| BRM-2 | Phantom 3ms unit cost invalidated EarnedRelightApplyCap |
| BRM-3 | Producer ≫ consumer (fifo~62, apply_n=1) |
| BRM-4 | stream_ms~65 vs phase budget 24ms |
| BRM-5 | FZ2.5 producer floors increased opaque_refs without consume gain |

### Tracks shipped

| Track | Change |
| --- | --- |
| Perf-0 | `ApplyBinding` telem, light/install ms split, `visible_black_focus_raw_n` |
| Perf-1 | Idle consume slice 16ms, light-unit earned cap, atomic timing split |
| Perf-2 | Consumer-bound suppress bg_floor/mesh_schedule when apply_n≤1 |
| Perf-3 | Stream hitch EMA for movement clamp |
| P0-A | VB SoT full scan + hysteresis (no telem ticketed early-out) |
| P0-B | Stalled → mesh_drain priority over mesh_schedule |
| P1 | `ShouldDeferRepairReticketUntilGpuApplied` |

### Validation infra

- `bin/tmp_fz26_step_validate.py` — per-track gates + no-teleport autofly
- `bin/tmp_fz26_delta.py` — segment delta vs 125933
- `bin/tmp_fz26_metric_audit.py` — post-PASS metric validity
- Extended `bin/tmp_fz2_gate_check.py` FZ26 gates

**Validate C8:**

```text
python bin/tmp_fz26_step_validate.py --step FZ26-C8 --build --scenario fz-manual-long
python tools/flight_sim_suite.py --only fz-manual-plateau fz-manual-long fz-manual-parity --phase-id FZ26-C8
```

---

## FZ2.7-B — MarkRelit refactor + O(1) light revision (2026-08-23)

**Baseline:** `perf_20260823-160343_792.jsonl` (post-FZ2.6)  
**Strategy:** Plan B full refactor (slim fast-path rejected)

### Architecture shipped

| Module | Role |
| --- | --- |
| `MeshLightStalePolicy.h` | O(1) `IsMeshLightStale` / GPU variant |
| `UChunk.LightFieldRevision` | bump on Apply light merge |
| `ChunkGreedyMesh.MeshedLightRevision` | set at mesh CPU/GPU commit |
| `RelightInstallPlanner.h` | pure `PlanColumnInstall` + path enum |
| `MarkRelitInstall.cpp` | orchestrator + `ExecuteLitApplyPlan` |
| `RelightFifoPolicy` B5 | cap math `light_ms + install_ms` |

### Tests (51+)

| Target | File |
| --- | --- |
| `mark_relit_characterization_test` | C01–C16 policy/path golden |
| `mesh_light_stale_policy_test` | revision stale O(1) |
| `relight_install_planner_test` | PrimaryConsume + FakeMesh |
| `mark_relit_integration_test` | I01/I04 cap simulation |

**Smoke:** `python bin/tmp_fz27_b_test_smoke.py`

### Tier 1 gates (validate on next fz-manual-long)

| Gate | 160343 | Target |
| --- | --- | --- |
| unit_apply_install_ms | 18.52 | **< 8** |
| relight_apply_n_steady | 1 | **≥ 2** |
| apply_util_steady | 0.05 | **≥ 0.15** |
| sim_steady | 129 | ≤ 135 |

```text
python bin/tmp_fz2_gate_check.py bin/logs/perf_<new>.jsonl
python bin/tmp_fz26_step_validate.py --step FZ27-B6 --build --scenario fz-manual-long
```

---

## FZ2.7-B1b — snapshot diet + stale probe zero + install telem (2026-08-23)

**Parent:** `cf31d209` (FZ2.7-B refactor)

### Shipped

| Change | Detail |
| --- | --- |
| `FillLitApplyMeshProbe` | Single GreedyCache lookup per chunk (was 8–10 MeshService calls) |
| Stale O(1) | `ChunkHasStaleDarkFaces` removed from MarkRelit hot path; `IsMeshLightStale` / `IsMeshLightStaleGpu` |
| Spike telem | `stale_probe_n`, `mark_relit_path_primary_consume_n`, `mark_relit_snapshot_ms`, `mark_relit_plan_ms`, `mark_relit_exec_ms` |
| Interim log | `perf_20260823-180432_20576` (~410s): install 19.65ms FAIL, enter_no_ticket 4 PASS |

### Validate

```text
python bin/tmp_fz27_b_test_smoke.py
python bin/tmp_fz2_gate_check.py bin/logs/perf_<new>.jsonl
```

**Next:** fz-manual-long ≥600s; target `unit_apply_install_ms < 14` interim, then `< 8` (B6).

---

## FZ2.7-B1c/d — install forensics + slim path + apply_n fix (2026-08-23)

**Interim log:** `perf_20260823-195843_35224` (~304s) — install 19ms, exec 0.02ms → fat outside snapshot/exec.

### Shipped

| Change | Detail |
| --- | --- |
| Install sub-timers | `mark_relit_total/orphan_ground/neighbor_seam/prefetch/mark_dirty/...` in spike JSON |
| Primary slim path | `primary_only && !enter` → `PlanPrimaryConsume` (not full Standard) |
| Skip orphan ground | `MarkTerrainChunkMeshDirtySeamed*` skipped on `primary_only \|\| consume` |
| apply_n bugfix | `ShouldStopRelightApplySlice`: `likely_cap = max(min_cap, slice/unit)` — was 0 when unit > slice |

### Validate

```text
python bin/tmp_fz27_b_test_smoke.py
python bin/tmp_fz2_gate_check.py bin/logs/perf_<new>.jsonl
```

**Gate-of-record:** fz-manual-long ≥600s required for B6 closeout.

**Manual gate-of-record:** `perf_20260823-211810_10940` (~780s). install med **16.7ms** (↓ from 19), apply_n max=3 med=1, orphan=0, opaque_refs **264 PASS**. Forensic: ~16.9ms mark_relit_total with sub-timers ~0.04ms → blind cost = `CountEnterFovLitDebt` on cruise.

---

## FZ2.7-B1e/f + B2b + B3 — setup gate + band filter + defer throughput (2026-08-23)

**Parent:** B1c/d gate `211810` — install still >8ms on steady cruise.

### Shipped

| Change | Detail |
| --- | --- |
| B1e setup gate | `CountEnterFovLitDebt` only when `enter_gate`; telem `mark_relit_setup_ms`, `mark_relit_primary_column_ms`, `mark_relit_bands_n` |
| B1f band filter | `ShouldFilterMarkRelitBandsToPrimary` — skip neighbor bands on primary_only/slim |
| B2b defer throughput | `ShouldUseThroughputApplyCap(consume, defer_side)`; slice ≥12ms on defer; `throughput_mode` in earned_cap/stop |
| B3 enter slim | `ShouldUseEnterSlimInstallPath` — enter+primary_only → `PlanPrimaryConsume` |

### Validate

```text
python bin/tmp_fz27_b_test_smoke.py   # ALL PASS
python bin/tmp_fz2_gate_check.py bin/logs/perf_<new>.jsonl
```

**Next gate:** manual ≥600s — expect `mark_relit_setup_ms ≈ 0` on cruise, install <12ms, apply_n ≥2.

**Gate-of-record:** `perf_20260823-220902_34572` (~754s, commit `b280bfae`).

| Metric | 211810 | 220902 | Gate |
| --- | --- | --- | --- |
| unit_apply_install_ms | 16.7 | **1.14** | PASS |
| unit_apply_light_ms | 1.03 | **1.09** | PASS |
| mark_relit_setup_ms | 0 | **≈0** | confirm B1e |
| relight_apply_n steady med | 1 | **1** (p90=3, max=6) | FAIL |
| apply_util steady | 0.05 | 0.05 | FAIL |
| apply_binding CountCap% | ~82% FatUnit | **67% CountCap** but gate 19% | FAIL |
| opaque_refs steady | 264 | **705** | FAIL (regression) |
| sim_steady | 147 | **116** | PASS |
| wall steady med | 142 | **117** | improved |
| stream steady | 61 | 50 | FAIL |
| revisit steady | 152 | 205 | FAIL |
| VB steady | 81 | 81 | FAIL |

**Verdict:** Tier-1 install **closed** (B1e root cause fixed). Throughput still blocked: cheap units disable `defer_side` → `throughput_mode` off → 1@8ms TimeSlice. Slim path side effect: `opaque_refs` +705 (skipped orphan/seam).

---

## FZ2.7-B2c — cheap-unit throughput latch (2026-08-23)

**Parent:** gate `220902` — install PASS but apply_n steady med=1.

### Shipped

| Change | Detail |
| --- | --- |
| Throughput latch | `ShouldUseThroughputApplyCap` — cheap unit (≤slice/2), ready≥2, fifo≥2/3 cap, PL pressure |
| Cruise slice | `RelightThroughputSliceMs` — ≥12ms when throughput on cruise |
| Apply budget | `CruiseRelightApplyBudget` — cap≥2 for unit<4ms even without fifo_pin_stable |

### Validate

```text
python bin/tmp_fz27_b_test_smoke.py   # ALL PASS
python bin/tmp_fz2_gate_check.py bin/logs/perf_<new>.jsonl
```

**Target:** apply_n steady med≥2, apply_util≥0.15, apply_binding CountCap≥80%.

---

## FZ2.7-B2d — min cap=3 + slice×3 for apply_util (2026-08-23)

**Parent:** gate `225006` — apply_n=2 PASS but apply_util=0.10 (need≥0.15).

### Shipped

| Change | Detail |
| --- | --- |
| Min cap 3 | `RelightThroughputMinApplyCap` — cheap unit + fifo/PL backlog → min 3 applies |
| Slice widen | `RelightThroughputSliceMs` — max(12, cap_unit×3), cap 16ms moving |
| Apply budget | `CruiseRelightApplyBudget` — unit<5ms → budget≥3 |
| Backlog helper | `RelightThroughputHasBacklog` shared by latch + min cap |

### Validate

```text
python bin/tmp_fz27_b_test_smoke.py
python bin/tmp_fz2_gate_check.py bin/logs/perf_<new>.jsonl
```

**Target:** apply_util steady ≥0.15 (med apply_n≥3).

---

## FZ2.7-P0 — SoT after B2d gate-of-record `101316` (2026-08-24)

**Parent:** `a07eb8db` (B2d). **Gate-of-record:** `bin/logs/perf_20260824-101316_32932.jsonl` (~520s, no-teleport).

### Compare (steady 120+)

| Metric | 220902 (B1e) | 225006 (B2c) | **101316 (B2d)** |
| --- | --- | --- | --- |
| apply_n med | 1 | **2 PASS** | **2 PASS** |
| unit install | **1.14 PASS** | hold | **0.05 PASS** |
| unit light | — | **2.7** | **6.50 FAIL** (&lt;5) |
| apply_util n/20 | — | 0.10 | 0.10 FAIL |
| sim med | — | — | **134 PASS** (kill 135) |
| opaque_refs | 705 FAIL | 555 | **255 PASS** |
| mark_relit | — | — | **2 PASS** |
| binding | — | — | TimeSlice+Fat 0.64 (old BRM ≥0.80) |
| VB / blink | — | — | 96.5 / 0.57 FAIL |
| stream / revisit | — | — | 51 / 231 FAIL |

### Forensic (not a code bug in light engine)

- Bimodal apply_n {1 vs 3}: `min_cap=3` beats `time_cap=2` when unit≈6.5ms in 16ms slice.
- Drain flags: `stopped_by_time |= applied>=earned` → cap-stop classified TimeSlice.
- `RelightApplyLightMs` billed `DrainCompleted` + merge (`iter_t0` before pop).
- PL backlog thresh 45 vs 101316 PL med≈32 → `RelightThroughputHasBacklog` often false on PL.

### Gate script (this phase)

- `apply_binding_countcap_pct`: CountCap share among `apply_n≥1` ≥ **0.50** (replaces TimeSlice+Fat ≥0.80).
- Diagnostic: TimeSlice/Fat print + honest util `n/min(ready,20)` when ready fields exist.

Autofly: none (docs/gates only). Next: P0b harness, then A time_cap wins.

---

## FZ2.7-A — time_cap wins (autofly plateau `132116`)

**SHA:** `1086e568`. **Autofly:** `perf_20260824-132116_4276.jsonl` (~224s wall, plateau).

| Gate | 101316 | 132116 plateau | Notes |
| --- | --- | --- | --- |
| CountCap share | 0.32 FAIL | **0.97 PASS** | A in-scope |
| unit light | 6.50 | **1.44 PASS** | B timers+skip (shipped same SHA) |
| unit install | 0.05 | **0.02 PASS** | hold |
| sim | 134 | **121 PASS** | hold |
| apply_n steady | 2 | 1 | late plateau drain (PL=1); not A FAIL |

No Classify commit-2 (CountCap already ≥50%). Light FAIL of 101316 is **out of A scope** but already green on this log.

---

## FZ2.7-B — light diet (same SHA as A, telem split)

`RelightDrainCompletedMs` / `RelightMergeLightMs`; `RelightApplyLightMs` = merge only. GPU-sky path skips `MergeBlockLightKeepingGpuSky` when block nibbles match (`BlockLightUnchanged`).

**Long A2:** `perf_20260824-134553_23644.jsonl` — unit light **0.55 PASS** (vs 6.50). Install **0.02**.

---

## FZ2.7-C — rate match

Producer suppress gains `light_unit > slice/3`. Capture `bg_cap` clamped when fifo ≥2/3 cap and completed&lt;2. **Do not** restore min_cap=3.

Long 134553: apply_n med **1**, util **0.05**, honest_util **1.0** (ready≈1). C DoD util≥0.15 **FAIL** — completed queue empty, not Drain min_cap. Light&lt;5 so C was in scope; no second min_cap revert.

---

## FZ2.7-D — VB / blink

Published VB deadband hold. `RepairReticketDeferredN` when GPU apply in flight. Extra inflight-GPU keep **reverted in A2** (opaque 771 on first long).

Long 134553: VB **6 PASS**, blink **0.47 FAIL**, opaque **732 FAIL**. Autofly residency ≠ 101316 manual.

---

## FZ2.7-E — Dirty coalesce

Far Dirty 1–2 frame delay **reverted in A2** (sim kill-switch). Inflight `RemoveAt` / schedule dedup unchanged. Revisit **103** (target&lt;95) FAIL. Stream **43** FAIL.

---

## FZ2.7-F — enter ticket seed

Enter Capture `bg_cap` 2/3 when no_ticket; enter-peak second pass remain≤4. **Not** `CountEnterFovLitDebt` on cruise.

**Parity:** `perf_20260824-135901_19400.jsonl` — enter_wall_p90 **156 PASS**, no_ticket_peak **113 FAIL**, PL_enter **36.5 FAIL**.

---

## FZ2.7-G — closeout (autofly long, kill-switch)

**SHA:** `7f045332` (A2 after `1086e568`).
**Long:** `bin/logs/perf_20260824-134553_23644.jsonl` (~650s scenario, ~1538s wall).
**Plateau:** `132116`. **Parity:** `135901`. First long (pre-A2): `132611` sim 144 / opaque 771.

### vs 101316 (delta)

| Metric | 101316 | 134553 A2 |
| --- | --- | --- |
| CountCap | 0.32 | **1.00 PASS** |
| unit light | 6.50 | **0.55 PASS** |
| unit install | 0.05 | **0.02 PASS** |
| VB | 96.5 | **6 PASS** |
| sim | 134 | **142 FAIL** kill-switch &gt;135 |
| apply_n | 2 | 1 |
| opaque | 255 | 732 FAIL |
| blink | 0.57 | 0.47 FAIL |
| sticky | 0 | **0 PASS** |
| enter_wall_p90 | 183 | long 257 FAIL / parity 156 PASS |
| uf_flips | 0 | **0 PASS** |

vs `114401`: sticky 0 hold; uf 0 hold; enter_wall parity better, long enter_p90 hitch.

**Kill-switch:** two long autofly iterations; sim_steady still &gt;135. No further slice grow. **Autofly GO is not G closeout** — one **manual ≥600s** still required.

Smoke: `python bin/tmp_fz27_b_test_smoke.py` ALL PASS at A ship.

---

## Manual `141417` (~594s) — true G gate vs autofly

`bin/logs/perf_20260824-141417_11404.jsonl` after `7f045332`.

| Gate | 101316 | autofly 134553 | **manual 141417** |
| --- | --- | --- | --- |
| sim | 134 | 142 FAIL | **134.8 PASS** |
| stream | 51 | 43 | **23.9 PASS** |
| light unit | 6.50 | 0.55 | **0.40 PASS** (p90≈5) |
| CountCap | 0.32 | 1.00 | **1.00 PASS** |
| apply_n / util | 2 / 0.10 | 1 / 0.05 | **1 / 0.05 FAIL** |
| completed med | 1 | 0 | **0** (fifo 55) |
| VB / blink | 96.5 / 0.57 | 6 / 0.47 | **98 / 0.52 FAIL** |
| opaque | 255 | 732 | **704 FAIL** |
| revisit | 231 | 103 | **227 FAIL** |
| PL enter | 31 | 30 | **22 PASS** |
| sticky | 0 | 0 | **0** |

Forensic: Capture `depth_cap = min(inflight, apply_n_prev+1)` even in consume — earned budget was 2 so completed never filled. GPU-sky skip still `Bump+MarkRelit`. Far FullyDark bypassed in-flight Dirty skip.

---

## FZ2.7-C2/E2/F2 tails (post-141417)

| Tail | Change |
| --- | --- |
| C | `RelightCapturePipelineDepthCap` — cheap+backlog depth 4–6, not `n_prev+1`. Backlog fifo≥cap/2 or fifo≥24@ready≤1, PL>30 |
| B hold | GPU-sky: MarkRelit only if light bytes changed vs pre-seed snapshot |
| E | far in-flight FullyDark skip re-Dirty (near FullyDark still immediate) |
| D | published VB deadband 2→3 |
| F | enter repair cap seed ≤12, collect ring 3 (not clamp 4 / ring 2) |

Slice moving still ≤16. min_cap=3 not restored as force when time_cap&lt;3.

**Plateau C2** `perf_20260824-144630_11460.jsonl`: sim **120 PASS**, CountCap **1.0**, light **0.59**, install med **0.00** (noop GPU MarkRelit), opaque 530 (лучше 704). apply_n med still 1 on drained plateau (PL=1) — rate-match needs другой ручной ≥600s.

---

## FZ2.7-P1–P4 (post-145926 flicker / far holes)

Eye: chunk blink + holes on long cruise. Steady `145926`: `mark_relit=0` every frame, VB 104, blink 0.61, `schedule_skip` 80 vs ok 5.5.

| Track | Change |
| --- | --- |
| P1 | `ShouldForceMarkRelitOnUnchangedLight` — GPU-sky packed noop still MarkRelit on consume / VB>40 / ticket / FullyDark LitDrawable. Slim empty-relit path no longer swallows repair. |
| P2 | `ClampCruiseDrainToReadyCheap` + consume slice 8→16 when cheap+ready≥2. Earned `min(ready,4)` vs 16ms time_cap. `min_cap=3` still does not beat fat time_cap. |
| P3 | `ShouldSkipInFlightDirtyReschedule` — FirstMesh holes never skip; near FullyDark nh≤2 re-Dirty; far remesh coalesces. |
| P4 | `ShouldHideFullyDarkOverLiveGpu` — hide-until-lit cannot drop live GPU nh≤4 (C5). Published VB deadband stays 3. |
| P5 | Closeout: one **manual ≥600s** World_164 far cruise. Autofly VB is not SoT. Compare `101316` / `145926`. |

Hold: sim&lt;135, moving slice ≤16, CountCap, no Starlight rewrite, no neighbor fanout in hot MarkRelit.

---

## FZ2.7-P6 tails (post-154945 flicker / ocean never-close)

Eye: blink + empty chunks + ocean planes that never close. SoT `154945` (~376s): MarkRelit partial (37/128), **89 apply frames SkipAlreadyDirty**, Dirty FM ~150, **fifo_drop 117→467**, unfinished 15–33 plateau.

| Tail | Change |
| --- | --- |
| Dirty bump | `ShouldBumpDirtyHeadForVisualHole` — FullyDark / missing mesh already-Dirty still `MarkDirtyPriority` (FirstMeshQ head) on consume or horiz≤8 |
| FIFO protect | `ShouldProtectRelightFifoTrimVictim` protect horiz **8** (was LitDrawable 4) on TrimFar + enqueue overflow |
| Capture refill | `RelightCaptureBgFloorForFifoStarve` bg≥3 when fifo≥50/2 and cheap; depth≥4 when fifo≥50 & ready≤1 |
| Remesh under holes | GPU pending>4 keeps remesh_cap **2** (was 1) so FullyDark remesh is not starved |

Hold: sim&lt;135, moving slice ≤16, no min_cap=3 over fat time_cap. Closeout still manual ≥600s.

---

## FZ2.7-P7 tails (post-162715 same flicker / unclosed holes)

Eye: P6 telem moved, picture did not. Manual `162715` (~628s): blink **0.57** (worse), unfinished 9–31 oscillating, Dirty FM ~196, skip=76 locked, apply=1 / completed=0, unlit_hidden 8–27 vs no_mesh 10–31 anti-correlated. fifo_drop plateau 241.

| Tail | Change |
| --- | --- |
| Noop remesh | `ShouldRemeshAfterLitApplyForHole` / bump only if missing mesh or light **rev ahead**. FullyDark + matching revs skip remesh (GPU-sky noop blink). |
| Cheap unit | `RelightApplyUnitIsCheap`: (0, slice/3] including 0.07ms. Capture fifo-starve floor and depth≥4 also when unit **unknown 0**. Drain lifts when ready≥2 unless expensive. |
| Live GPU keep | `ShouldKeepLiveGpuOpaqueDespiteFullyDark` horiz **8** (`kVisualStageProtectHoriz`), not LitDrawable 4. |
| FirstMesh SoftDefer | missing mesh nh≤8 schedules despite SoftDefer (ocean planes). |

Hold: sim&lt;135, moving slice ≤16, min_cap=3 still does not beat fat time_cap. Closeout: one **manual ≥600s**.

---

## FZ2.7-P8 REJECTED (manual `213856`)

Uncommitted P8 (MarkMissing every slim Apply + fifo Apply floor without ready):
opaque **660→279**, unfinished 15→19, unlit 8→16, **sim&gt;135 on 32%** frames (max 654).
Violates BEST_PRACTICES / industrial RelightThenMesh (producer boost without Completed).
**Reverted to P7 `8b16a492`.** Follow-on: FZ2.7-P9.

---

## FZ2.7-P9 (post-P8 reject / RelightThenMesh repair)

| Track | Change |
| --- | --- |
| R0 | WT = P7; P8 discarded |
| P9-0 | `mark_relit_invoked_n` vs schedule; emit `capture_bg_cap_n` / `capture_band_cy`; Dirty skip pipeline/snapshot/softdefer/locked |
| P9-A | `ShouldSuppressProducerBoostWhenConsumerBoundP9` (no suppress when completed=0 + fifo starve); HighPl Apply floor only if ready≥1; `sim>135` kills Capture/Apply boost (`SimMsPrev`) |
| P9-B | `ShouldMarkMissingOnceOnLitReady` — one-shot MarkMissing / FirstMesh when schedule_n=0 + no_mesh debt; keep PL if still !drawable without FM |
| P9-C | SoftDefer leave-in only for drawable under PL (`ShouldLeaveInDirtyUnderPlForSchedule`); FM cap untouched |

Smoke: `bin/tmp_fz27_b_test_smoke.py` ALL PASS. Release `Cubatarium.exe` rebuilt.
**Manual SoT:** `091745` (~300 s, short of ≥600). Verdict: **not gate**. opaque med **212** (FAIL vs P7 660 / DoD &lt;400); sim med 112, sim&gt;135 **20%** (better than P8 32%); unfinished tail 4–7 but Dirty FM plateau ~170; Capture cap often 1 under fifo starve; `mark_missing_primary=0` (no storm). See canvas `manual-091745-p9`.

Hold: P7 noop-remesh + keep GPU nh=8. Autofly ≠ SoT; sim&lt;135 hard.

---

## FZ2.7-P10 (post-091745 Capture/keep/FM)

Root from `091745`: `capture_hard_cap=1` under holes killed Completed refill; keep RD shrink 169↔121 demoted opaque (not hide); Dirty FM skip mostly unattributed.

| Track | Change |
| --- | --- |
| A1–A3 | `RelightCaptureBgFloorForFifoStarve` floors when `completed=0` even if inflight high; SoftDefer depth-full refill; `ClampCaptureBgAfterSimKill` keeps refill≤3; MemoryBudget holes hard_cap **3** when fifo starve+completed empty; restore floor after hard_cap in WorldStreaming |
| C1 | no RD shrink under holes (`max_effective_rd = visual_rd`); hold RD while unfinished&gt;0 |
| B0–B2 | skip orphan/remesh_starve/other telem; orphan RemoveAt; PreferKick pending GPU for FM protect before Pass1 |
| C2 | GPU keep ring → protect horiz 8; `freechunk_live_n` / `keep_ring_shrink_n` telem |

Closeout: smoke + Release; manual ≥600s vs `091745` / `194347`.

---

## FZ2.7-P11 (post-110448 Capture refill + fifo trim)

Root from `110448`: opaque/keep/FM OK; **Capture cap med still 1** (hot SoftDefer clamp after floor); `fifo_drop Δ=64`; `completed med 0`.

| Track | Change |
| --- | --- |
| A1 | `ShouldBypassCaptureHotSoftDeferClamp` — fifo starve + Completed empty skips hot clamp to 1 |
| A2 | Final `RelightCaptureBgFloorForFifoStarve` after all late clamps in DrainRelightQueues |
| A3 | MemoryBudget hard_cap **3** on completed_starve even under memory_pressure 1/2 and dirty>600 |
| B1 | `RelightFifoEffectiveTrimProtectHoriz` — consumer starved → trim protect nh=4 not 8 |
| B2 | `ShouldCruiseRedFifoSecondTrim` — no aggressive 3/4-cap trim when Completed empty |
| B3 | Telem: `relight_fifo_overflow_drop_n`, `relight_fifo_protect_block_n` (overflow vs trim_far vs protect stall) |

Closeout: smoke + Release; manual ≥600s vs `110448` / `194347`.

---

## FZ2.7-P12 (post-120512 publication debt — plan)

Root from `120512`: Capture/Apply OK (cap med **3**, apply med **3**); **unfinished/no_mesh/unlit/plf** plateau **~70–88** (P10 late **18/3**, P7 **~15/8**). Chain break: Apply→MarkRelit **skip_already_dirty** → **no FM schedule**; Dirty **Remesh-dominated** (fm med **16** vs remesh **98**); SoftDefer empty **age_max ~3227**; `pending_light_dropped Δ=2882`; meshing−ready gap med **63**.

**Non-goals:** Capture++ / fifo cap++ / hide policy / MarkMissing storm / keep demote.

### Gate manual (≥600 s vs `120512` / `110448`)

| Metric | P11 `120512` | P12 target |
| --- | --- | --- |
| late `unfinished_visual` | ~80+ | **≤25** (P10-like) |
| late `chunk_meshed_unlit_hidden` | ~70 | **≤15** |
| late `pending_light_focus` | ~70 | **≤20** |
| `column_meshing_n − column_render_ready_n` med | 63 | **≤35** |
| `dirty_fm_n / column_loaded_no_mesh_n` | 0.25 | **≥0.8** |
| `mark_relit_schedule_n` med | ~0 | **≥1** when apply≥2 |
| `pending_light_dropped` Δ/spike | ~6 | **≤2** |
| `softdefer_witness_retarget` Δ/spike | ~10 | **≤3** |
| opaque med | 687 | **≥650** hold |
| capture_bg med | 3 | hold (no Capture++) |

---

### Track A — FM admission for no_mesh / SoftDefer-empty

**Hypothesis:** lit Apply clears PL tickets on columns already in Dirty as **Remesh**, not **FirstMesh**; no_mesh holes never enter FM Pass1 at sufficient rate.

| Step | Where | Change |
| --- | --- | --- |
| A0 | telem | Per-frame: `mark_relit_skip_already_dirty_n`, `mark_relit_enqueue_first_mesh_n`, `dirty_fm_n`, `dirty_remesh_n`, `softdefer_empty_stuck_n` already in jsonl — add audit script gate rows |
| A1 | `RelightInstallPlanner.h` / `MarkRelitInstall.cpp` | When `skip_already_dirty` **and** column **!HasGreedyMesh** (or SoftDefer-empty owned): **force `enqueue_first_mesh`** or `MarkDirtyPriority` as **FirstMesh class** (not remesh suffix). Unit: skip_dirty + no_mesh → schedule FM |
| A2 | `MeshWorkAdmission.h` | New guard `ShouldStealRemeshToFirstMesh(unfinished_visual, no_mesh_n, remesh_queue_n)`: when `holes && unfinished>30 && fm/no_mesh<0.5` → `remesh_schedule=0`, `first_mesh_schedule+=steal` even if `remesh_queue_n>0` (inverse of current Era18 branch that keeps remesh≥1) |
| A3 | `ChunkMeshCache.cpp` Pass1 | Extend P10 PreferKick loop: for `SoftDeferEmptyStuck` coords from telem / `IsSoftDeferHeld && !HasDrawableGreedyMesh`, call `PreferKickPendingGpuQueued` **or** `MaybeMarkDirtyAfterSoftDeferEmptyAvoid` with **cd=0** under `unfinished>40` |
| A4 | `SoftDeferEmptyPolicy.h` | Widen `ShouldPreferKickSoftDeferEmptyStuck`: allow kick when `softdefer_empty_stuck_n>0 && !HasDrawableGreedyMesh` even if GPU not queued (age≥`ShouldEscalateSoftDeferEmptyAge`) — cap **2/frame** |
| A5 | `ChunkMeshCache.cpp` Dirty dual-Q | Under `StarveRemeshForHoles`: if chunk **!HasGreedyMesh** in LitDrawable ring, **promote to FirstMesh prefix** (not remesh suffix) on MarkRelit / SoftDefer heal |
| A6 | `ChunkEmergeCoordinator.cpp` | When `visual_holes && unfinished>30`: ensure `SetStarveRemeshForHoles(true)` for full emerge tick (verify not cleared early at L1531/L1834) |

**DoD A:** `dirty_fm_n` med **≥40** OR `fm/no_mesh ≥0.8`; `mark_relit_schedule` sum/spike **>0** steady; meshing−ready gap **shrinking** in timeline after ~300 s.

---

### Track B — PL trim / churn under holes

**Hypothesis:** `TrimPendingLightBeforeMesh` + MemoryBudget trim drop **LitReady+has_mesh far** tickets while Capture keeps re-noting PL; focus no_mesh tickets survive but system churns (`relight_note_skipped_dup`, `pending_light_dropped`) without clearing focus debt.

| Step | Where | Change |
| --- | --- | --- |
| B0 | telem | Split cumulative: `pending_light_trim_emerge_n`, `pending_light_trim_memory_n` (per-frame counters) |
| B1 | `ChunkEmergeCoordinator.cpp` ~1464 | Gate trim: `ShouldTrimPendingLightUnderHoles(visual_holes, unfinished, pl_focus)` → **false** when `holes && (unfinished>20 \|\| pl_focus>15)` |
| B2 | `WorldStreaming.cpp` ~2859 | Same gate for MemoryBudget `dirty>400` trim path |
| B3 | `World.cpp` `TrimPendingLightBeforeMesh` | Never drop within `LitDrawable+4` protect ring (not just dist≤1); align with FIFO trim protect |
| B4 | Capture/Apply | Audit `NotePendingLight` on void enqueue / finalize — if duplicate with existing PL on **same column without mesh progress**, increment suppress counter (extend `relight_note_suppressed_plateau`) |
| B5 | optional | Raise effective soft_cap under holes only for **global** PL (not focus) — **last resort** if B1–B4 insufficient |

**DoD B:** `pending_light_dropped` Δ/spike **≤2**; late `pending_light_focus` **≤20**; no regression `relight_fifo_drop`.

---

### Track C — SoftDefer witness pin / retarget thrash

**Hypothesis:** `softdefer_witness_retarget Δ≈10/spike` + `empty_age_max≈3227` → Capture witness hops before FirstMesh completes; `SoftDeferCapturePinAge` resets stall heal.

| Step | Where | Change |
| --- | --- | --- |
| C0 | telem | Log `softdefer_capture_pin_age`, `softdefer_witness_horiz` per spike (if missing) |
| C1 | `AntiFlickerPolicy.h` | `ShouldRetargetSoftDeferCaptureWitness`: if `softdefer_empty_stuck_n≥1` at pin coord → **extend pin_T** to `max(pin_T, 24)` while `!HasDrawableGreedyMesh` |
| C2 | `WorldStreaming.cpp` ~1929 | `better_horiz` retarget: require **Δhoriz≥2** (not 1) when `unfinished_visual>30` |
| C3 | `WorldStreaming.cpp` | When `SoftDeferEmptyStuckN>0`, **prefer stuck witness** over miss witness for pin — do not retarget away until drawable or age>extended_T |
| C4 | `RelightFifoPolicy.h` | `ShouldHoldPinnedRelightWitness`: extend to nh≤**4** (LitDrawable) when `softdefer_empty_stuck` at pin |
| C5 | land frontier | `ShouldDampLandFrontierWitnessRetarget`: also damp when `player_z` moving (P11 z 52→80 cruise) and `unfinished>40` |

**DoD C:** `softdefer_witness_retarget` Δ/spike **≤3**; `softdefer_empty_age_max` p90 **<1500** in late steady; stuck_n med **≤3** at tail.

---

### Implementation order

1. **A1 + A2** (MarkRelit→FM + admission steal) — highest leverage, smallest behavioral surface  
2. **B1 + B2** (stop PL trim under holes) — quick, reversible  
3. **C1 + C3** (pin stability) — pairs with A3/A4  
4. **A3–A6, B3–B5, C2/C4/C5** — tune from first manual  
5. Smoke + Release + journal closeout

### Risks / guards

- A1 FM storm: cap `enqueue_first_mesh` **≤ apply_n** per frame; no MarkMissing  
- A2 remesh=0: only when `remesh_queue` far-only or `unfinished storm`; keep remesh≥1 nh≤2 stale-dark  
- B1 PL backlog RAM: monitor `pending_light` max; B5 fallback  
- C1 long pin: expire on `HasDrawableGreedyMesh` or `pin_age>48`

---

## FZ27-P12 Implementation (code land)

**Stamp:** implementation on `perf_opt11` (post-P11 `120512` SoT). Manual ≥600 s gate pending human run; unit + smoke + Release below.

### Landed

| Track | Change |
| --- | --- |
| A1 | `ShouldForceFirstMeshOnSkipAlreadyDirty` + skip branches → `MarkDirtyPriority` + `enqueue_first_mesh`; telem `mark_relit_enqueue_first_mesh_n` |
| A2 | `dirty_fm_n`/`no_mesh_n` → `ShouldStealRemeshToFirstMesh`; remesh=0 + FM steal; backpressure/light_debt skip remesh floor when steal |
| A3 | Pass1 PreferKick SoftDeferEmptyStuck / Held; age≥15 without GPU; MarkDirty avoid under unf>40; cap 2 |
| A4 | `MarkDirty` under StarveRemesh + !greedy LitDrawable → `MarkDirtyPriority` |
| A5 | StarveRemesh held while holes+unf>30 (idle remesh debt cannot clear mid-tick) |
| A6 | SoftDefer stuck FM enqueue under moving cruise when unf>30 + age>15 |
| B0 | `pending_light_trim_emerge_n` / `pending_light_trim_memory_n` per-frame + jsonl |
| B1/B2 | `ShouldTrimPendingLightUnderHoles` at emerge + MemoryBudget |
| B3 | Trim protect `RelightFifoTrimProtectHoriz()` (8) |
| B4 | duplicate NotePendingLight without mesh → `relight_note_suppressed_plateau` |
| C1–C5 | pin_T≥24 stuck; hard expire 48; better_horiz Δ≥2; stuck prefer; hold nh≤4; damp moving+unf>40 |

### Verification

- Unit: `miss_first_mesh_class_test` OK; `relight_install_planner_test` OK (P12 A1 hole→FM)
- Smoke: `python bin/tmp_fz27_b_test_smoke.py` → **ALL PASS**
- Release: `Cubatarium.exe` built (static verify PASS)
- Audit: `bin/tmp_forensic_120512.py` — DoD table helper (`dod_gate`); fill after manual ≥600 s

### Partial DoD (code) / Final gate (manual)

Code land complete (A1–A6, B0–B4, C1–C5). Capture untouched. **Final gate** metrics remain human manual ≥600 s vs `120512`/`110448`; journal metrics fill after that run.

---

## FZ27-P13 Lit-settle remesh protect

**SoT:** manual `154246` after P12 — PL/FM/Capture KEEP, but `dark_face_stale~3200`, `remesh_cap` sticky=1, unlit~76 (drawable FullyDark starved).

### Landed (Phase1 + Phase2 preempt from SoT sticky)

| Step | Change |
| --- | --- |
| R1 | `ShouldProtectLitSettleRemesh(holes, stale>200, remesh_q>0)` + `dark_face_stale_near_n` input |
| R2 | remesh floor≥2 over A2 steal / light_debt ceil; backpressure keeps floor |
| R3 | Finalize `StarveRemeshKeepHoriz = max(adm, stale_keep 2\|3)` |
| R4 | Cache prune/Pass2: stale FullyDark ≤ protect horiz=8 not starved |
| R5 | telem `remesh_protect_lit_settle_n` + jsonl |

**KEEP:** PL trim gate, A1 FM-on-skip-dirty, Capture, SoftDefer pin, A2 FM schedule boost.

### Verification

- Unit: `miss_first_mesh_class_test` OK (P13 protect + P12 steal without stale)
- Smoke: `python bin/tmp_fz27_b_test_smoke.py` → **ALL PASS**
- Release: `Cubatarium.exe` built (static verify PASS)
- Manual DoD vs `154246`: remesh_cap med≥2 when stale>200; dark_face_stale ↓; unlit ≤69 (better ≤40); PL drop≈0; capture≥3; dirty_fm≥40

---

## FZ27-P14 Frontier flicker closeout

**SoT:** manual `170807` after P13 — keep-up/stale OK; remaining eye flicker from `vb_no_ticket~39`, SoftDefer retarget ~4.2/spike, unlit ±1 frontier.

### Landed (then FAIL)

| Step | Change |
| --- | --- |
| F1 | Capture pin MaxAge preserve on retarget (`SoftDeferCapturePinMaxAgeAfterRetarget`) |
| F2 | Ingress SoftDeferWitnessRetarget damp under unfinished cruise / same-pin |
| F3 | `VisibleBlackNoTicketRepairCap` moving floor 4→6/8 |
| F4 | moving vb_no_ticket>20: repair_cap+2 + second Collect pass |
| F5 | publish deadband for `VisibleBlackNoTicketN`; raw for heal Collect |
| F6 | hold `ChunkMeshedUnlitHidden` publish 2 frames when \|Δ\|≤2 |

**KEEP:** P12 PL trim, P13 remesh protect, Capture.

### Verification (code)

- Unit: `miss_first_mesh_class_test` OK (P14 F1–F6 predicates)
- Smoke: `python bin/tmp_fz27_b_test_smoke.py` → **ALL PASS**
- Release: `Cubatarium.exe` built (static verify PASS)

### Results — manual `205739` (commit `07f6d745`, ~186 spikes) vs `170807`

| Metric (rest/late) | P14 `205739` | P13 `170807` | Gate |
| --- | ---: | ---: | --- |
| softdefer_capture_floor /spike | **19.6** | 6.7 | FAIL (truthful Site B) |
| softdefer_witness_retarget /spike | **11.4** | 4.2 | FAIL (↑ despite F2) |
| softdefer_empty_stuck med | **9–11** | ~4 | FAIL |
| dark_face_stale late | **41 sticky** | 5 | FAIL |
| vb_no_ticket late med | 23.5 | 40.5 | FAIL DoD ≤15 |
| unfinished late | 19 | 27 | PASS |
| capture / PL drop / keep | 3 / 0 / 169 | 3 / 0 / 169 | KEEP OK |

Eye: black chunks + SoftDefer thrash + flicker worse; player NE to ~(118,86) then standstill.

**Root cause:** F1 MaxAge ratchet feeds `pin_T` (`MaxAge → pin_T → max(prev,…)`) after land_frontier/stuck elevates MaxAge — sticky wrong Capture pin → floor/stuck storm. F5 deadband bleeds published `VisibleBlackNoTicketN` into budgets. F3/F4 are moving-only (cannot explain standstill). F2 only gates Site A count (poisons SLA).

### Decision

**FAIL — full revert** of `07f6d745` → `02f537ce` (back to P13 `9f502dd5` behavior). Repro: no-teleport `fz-ne-frontier-stand` / `fz-frontier-stand-resume` (audit `bin/tmp_audit_ne_frontier_stand.py`). P15: split SoftDefer metrics; optional F3+F4 re-land; F1 only with decay (no ratchet); never F5 in control.

### Repro scenarios (no-teleport)

| Scenario | Timing | Purpose |
| --- | --- | --- |
| `fz-ne-frontier-stand` | cold World_164 @~(118,86), idle30+fly0+stop120 | SoftDefer standstill at P14 fail locus |
| `fz-frontier-stand-resume` | resume near frontier, idle15+fly10+stop90 | standstill-only isolate |

Gates (stand window): floor/spike ≤8; witness/spike ≤5; stuck med ≤5; stale ≤20; KEEP capture≥3 / PL drop≈0 / keep≥160.

### A/B post-revert

| Run | floor/spike | witness/spike | stuck | SoftDefer gates |
| --- | ---: | ---: | ---: | --- |
| P14 manual `205739` @ (118,86) | 19.6 | 11.4 | 9 | **FAIL** |
| P13 manual `170807` | 6.7 | 4.2 | 4 | PASS |
| post-revert autofly `215042` @ (118,86) | **6.5** | 4.8 | 2 | **PASS** (≈P13) |

Operator: manual eye ≥180s vs `170807` still recommended (autofly ≠ visual merge).

---

## FZ27-P15 SoftDefer observe + moving repair (post P14 revert)

**SoT:** SoftDefer gates green on post-revert `215042` / P13 `170807`.

### Landed

| Step | Change |
| --- | --- |
| P15a | Split telem `SoftDeferIngressWitnessN` / `SoftDeferCaptureRetargetN` (legacy total kept); no count-gating |
| P15b | Re-land moving F3/F4 only (`VisibleBlackNoTicketRepairCap` floor 6/8 + repair_cap+2 + second Collect) |
| P15c | `SoftDeferCapturePinMaxAgeAfterRetarget` with decay to 8 / hard cap 24 — **no ratchet on prev** |
| — | **Never** F5 deadband in control; **never** F2 count-gating |

### Verification

- Unit: `miss_first_mesh_class_test` (P15c + P15b caps)
- Smoke: `python bin/tmp_fz27_b_test_smoke.py`
- SoftDefer: `fz-ne-frontier-stand` + `bin/tmp_audit_ne_frontier_stand.py` must stay PASS vs `215042` band

### Results — SoftDefer + manual residual

- SoftDefer A/B autofly `215042` @ (118,86): floor/spike **6.5**, stuck 2 → **PASS** ≈ P13
- Manual `221516` (commit `4b7ec652`): unlit late med **3** (flicker rare); keep=169; SoftDefer Site B retarget/spike **0.83** OK
- **Open:** sticky `focus_missing_mesh=1` almost entire cruise `(118,86)→(-31,58)`; transient unf/nm 30–75; rare in-ring holes (not SoftDefer carpet)

---

## FZ27-P16 In-ring focus holes (cruise underfeet / FirstMesh)

**SoT:** manual `221516` after P15 — SoftDefer OK; sticky miss + rare holes inside keep=169.

### Landed

| Step | Change |
| --- | --- |
| U1 | `ShouldPinIsolatedMissUnderfeet` — nh≤1 pin in cruise (drop `!moving`) |
| U2 | `ShouldEnqueueWitnessOwnedFirstMesh` — miss && nh≤2 && !drawable → FirstMesh `scan_full_focus=false` |
| U3 | SoftDefer audit Site B SoT; `fz-inring-cruise` repro |

**KEEP:** P13 remesh protect, Capture, P15c MaxAge decay, SoftDefer standstill gates ≈ `215042`.

### Verification

- Unit: `miss_first_mesh_class_test` (U1/U2 predicates)
- Smoke: `python bin/tmp_fz27_b_test_smoke.py`
- SoftDefer: `fz-ne-frontier-stand` KEEP vs `215042`
- Cruise: `fz-inring-cruise` + hole audit vs `221516` (`miss_sticky_frac` ↓ ≥30%)
- Manual ≥180s inside keep

### Results — autofly DoD

| Gate | Result | Evidence |
| --- | --- | --- |
| Unit + smoke | **PASS** | `miss_first_mesh_class_test` OK; `tmp_fz27_b_test_smoke.py` ALL PASS |
| SoftDefer standstill | **PASS** KEEP | `224052` vs `215042`: floor/spike **2.38** (≤8), Site B retarget **0.04** (≤5), stuck 3, capture 3, keep 169, PL drop 0 |
| Cruise hole `miss_sticky_frac` ↓≥30% | **FAIL** | `225326` (`fz-inring-cruise` teleport 118,86 yaw180): sticky **1.000** (same as `221516`) |
| Cruise `moving_unf_p90` / `nm_p90` | **PASS** ↓ | unf 66→**32**, nm 66→**32**; keep_med 169; unlit_late 0 |
| SoftDefer on cruise rest | OK | Site B retarget/spike **0.17** |

**Verdict: P16 PARTIAL FAIL** — SoftDefer regress ban green; pulse holes (unf/nm) improved; **sticky `focus_missing_mesh` not cleared** (likely mid-rim mh>2 witness — outside U1 nh≤1 / U2 nh≤2 ownership). Follow-up **P17 admission** (`IsNearFocusMissUrgent` mh≤4), not expand P16 SoftDefer/Capture scope.

**Scenario note:** cold World_164 save was at `(-31,58)` after manuals; yaw 270 = south → ocean keep~49. `fz-inring-cruise` pins SoT via `--teleport-cruise --cruise-cx 118 --cruise-cz 86 --yaw 180` (west).

**SHA:** `ea854043` (P16 landed). Manual SoT residual: `100413` (see P17).

---

## FZ27-P17 Stand discard / VB never-heal / mid-rim miss (post `100413`)

**SoT:** manual `perf_20260826-100413_20156.jsonl` after P16 — SoftDefer OK; long cruise+stand: wall~132, `stream_ms`~40, VB_focus **99 sticky**, `mesh_discarded_late` ~0.47/spike, unf↑ on stand, miss sticky nh~2–3.

### Root cause (forensics)

| Finding | Evidence |
| --- | --- |
| Discard = Forgot inflight | `MarkDirtyPriority` !Drawable Forgot when `!soft_undrawn` |
| Dirty thrash on stand | discard frames: dirty_touch 67–143, remesh_protect=1, orphan~45 |
| SoftDefer not root | Site B retarget/sp 0.05, floor 2.2 |
| Bisect note | Path pre-P16; long cruise left VB=99 → storm on `100413` stand (`221516` late discard rate 0) |

### Landed

| Step | Change |
| --- | --- |
| A | Hold supersede for FirstMesh miss undrawn (`miss_undrawn`) — stop Forget/discard |
| B | Stand VB full-scan cadence after raw stable (cut idle every-frame scan) |
| C | `IsNearFocusMissUrgent` / U2 nh≤4; stand column-owned FirstMesh when miss age>300 |

**KEEP:** SoftDefer Site B / floor ≈ `215042`; P13 remesh protect; Capture; P15c MaxAge decay.

### Verification

- Unit + `tmp_fz27_b_test_smoke.py`
- SoftDefer: `fz-ne-frontier-stand` + `tmp_audit_ne_frontier_stand.py`
- Stand: `fz-inring-cruise` stop≥120 + `tmp_audit_100413_stand.py` vs `100413`
- Manual ≥180s inside keep

### Results — autofly DoD

| Gate | Result | Evidence |
| --- | --- | --- |
| Unit + smoke | **PASS** | ALL PASS |
| SoftDefer KEEP | **PASS** | 104722 floor **2.08**, SiteB **0.03**, stuck 1, keep 169 |
| P0 discard stand | **PASS** | 104722 discard/spike **0.035** (SoT 100413 **0.47**); stop discard Δ **0** |
| P0 stale stand | **PASS** | late stale **0** (SoT **1755**) |
| P1 stream_ms | **PARTIAL** | SoftDefer stand stream med **58** (target ≤25); ocean-abort cruise late stream 17 invalid |
| P0 VB/unf falling | **FAIL** | SoftDefer stand vb late **116**, unf 67→85 (enter/stand residual) |
| Cruise land long-stop | **ABORT** | 105100 yaw180 fly60 → keep **49** ocean; fly default cut to **35s** |

**Verdict: P17 PARTIAL PASS** — discard/stale storm cured; SoftDefer KEEP; VB/unf/stream on long land stand still open (follow-up). SHA working tree atop ea854043.

**Bisect:** code-path forensics (not full binary A/B): MarkDirtyPriority Forgot when !soft_undrawn on !Drawable+inflight; 100413 discard frames 100%% remesh_protect + dirty_touch thrash.

---

## SRBR-P0 Ghost Dirty drain + underfeet miss ownership (post `112418`)

**SoT:** manual `perf_20260826-112418_14164.jsonl` after P17 SoftDefer KEEP — discard/stale cured; long land stand: `skip_orphan` med~54, `dirty_n` plateau~99, sticky miss `(-2,0,4)` nh=0, stream~41 (dirty_tick~27).

### Root cause

| Finding | Evidence |
| --- | --- |
| Ghost Dirty flood | schedule RemoveAt×~54/frame for `!HasChunk` |
| Re-admit without resident | MarkDirty* / pin_isolated_miss without HasChunk |
| Underfeet miss ownership | sticky nh≤1 !drawable without guaranteed FM when loaded |

### Landed

| Step | Change |
| --- | --- |
| A | `ShouldAdmitResidentDirty` + `SetChunkResidentFn(HasChunk)` on MarkDirty* |
| B | `PruneGhostDirty` bulk cap (64 holes / 24 else) before Dirty touch sample |
| C | `ShouldGuaranteeResidentWitnessFirstMesh` nh≤1 + P17 age>300 require HasChunk |
| D | Units in `MissFirstMeshClassTest` |

**KEEP:** SoftDefer Site B / floor; P13 remesh protect; miss_undrawn; MaxAge decay; no Capture++.

### Verification

- Unit + `tmp_fz27_b_test_smoke.py`
- SoftDefer: `fz-ne-frontier-stand` + `tmp_audit_ne_frontier_stand.py` vs `104722`
- Plateau: `fz-manual-plateau` (no stream/VB regress)
- Manual ≥180s land stand keep≥160: skip_orphan≤10, miss_frac≤0.5, unf late≤40, VB≤70, stream≤30, discard≤0.05

### Results

| Gate | Result | Evidence |
| --- | --- | --- |
| Unit + smoke | **PASS** | `miss_first_mesh_class_test` OK; smoke ALL PASS |
| Autofly SoftDefer | **PASS** (retry) | `134508` SoftDefer audit **OVERALL PASS** (floor 1.26, SiteB 0.03, stuck 4, keep 169); first `133715` stuck_med=9 flaky FAIL |
| Autofly plateau | **ABORT** | hang-kill 230s + truncated jsonl |
| Ghost Dirty metric | **PASS** | skip_orphan med **0** (112418 **54**); prune_n med **61–64**; dirty_tick **0.7–2ms** (was ~24) |
| Miss/VB heal | **FAIL** | miss sticky; VB late **109–111**; schedule_ok med **1–2** (was ~6) — expected residual → P1 |
| flight_sim gates | FAIL | wall/PL/EH fail (prep dominate); SoftDefer KEEP is SoT for this package |
| Manual stand DoD | pending | need ≥180s land stand keep≥160 vs `112418` |

**Verdict: SRBR-P0 PARTIAL PASS** — ghost Dirty drain + SoftDefer KEEP green; VB/miss/stream residual for **P1**. Working tree atop `1f3f7675`. Logs: SoftDefer `perf_20260826-134508_20740.jsonl` (PASS), first `133715` (stuck flaky).

---
