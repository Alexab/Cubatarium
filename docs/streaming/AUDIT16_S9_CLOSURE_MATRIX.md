# Audit 2026-09-16 S9 closure matrix

Source audit: commit `7a658525` / `docs/streaming/CURRENT_STATE_AUDIT_2026-09-16.md`.
Tails plan: H0–P5 on `cursor_audit2_impl`.
Full cutover: E0–E9 on `cursor_audit3_impl`.
Gap-closure: B0→R1→S5→S2→S4→S6–S8→S9 on `cursor_audit3_impl` (2026-09-17).

## Absolute gates (must be green)

| Gate | Owner step | Status |
|---|---|---|
| live/free allocation overlap = 0 | S1 `publication_audit` | **PASS** (generation ledger `28dfc64f`) |
| empty Replace / Remove last material | S2 | code + CTest |
| reorder bumps table identity | S2 | code + CTest |
| packed excluded when MDI resident | S2 GeometryEngine | code |
| RepresentationSwitch clears MDI | S2 `ApplyPublicationDelta` | code + CTest |
| **Single Replace writer** | S2 `PublishPassInputs`→`ApplyPublicationDelta` | code + CTest |
| **RefreshPassRefs resident filter** | gap S2 | code + CTest |
| eye-proxy absolute holes + missing fields | S0 | GateRepro |
| `operator_visual=None` ↛ merge_green | S0 scorecard | code |
| west route `cx≤−3` or UNTESTED | S0 | COVERED (autofly) |
| per-frame GPU kick/finish timers | S3 | code |
| oracle wrong-tex / checkerboard / liquid / temporal | S3 | OracleFixtures |
| source light stamp on publish | S4 | code (fail-closed) |
| **versioned boundary overlay → mesher** | S4 | code + CTest (raw shell + overlay emit) |
| per-column stalled remesh bookkeeping | S5 | code |
| draw-oracle age not reset on unrelated lit | S5 | CTest |
| **H2 FullyDark Dirty deleted** | S5 | code (block removed) |
| enter FullyDark Dirty pump OFF | S5 | code |
| Flow FullyDark census demand removed | S5 | code (stub returns 0) |
| snapshot credit lifetime with Store | S6 | hot-path + CaptureAndStore |
| shared work-slot concurrency + stress | S6 | `job_admission_lifetime_test` stress |
| spawn-ring cache world epoch + focus.y + unf/nr deps | S7 | code |
| bounded critical overrun (not infinite) | S7 | `FrameDeadline` |
| no independent 4ms/6ms GPU floors | S7 | frac-only / miss_reserved (no `max(6.0)`) |
| cull stats buffer-update barrier | S8 | code |
| frustum near/far AABB fixtures | S8 | `frustum_clip_test` real cases |

## Product acceptance

- Gate of record: `product-174657` **no-teleport** west.
- Autofly adequacy ≠ CLOSED.
- SLA 16.7/33.3 ms — proposed profile **not chosen**; not a gate.

### Gap-closure B0→S9 verdict: **OPEN-with-blockers**

| Signal | Result |
|---|---|
| absolute CTest gates | PASS |
| west COVERED + incomplete=0 + eye_proxy | PASS (cold×3 + warm×3) |
| west rim moving clnm (R1) | improved B0 **31→9** (vs manual ~15) |
| stop rim plateau ≤2 | **OPEN** (still elevated on stop) |
| dual-lane mid stalled ≤5 | **OPEN-with-cause** (cold ~53–57; accepted for merge as separate epic — not N04 remesh caps; not greenwash) |
| operator west mid+sea rim | **UNTESTED** (manual required) |
| continuous 10–15 min soak | **OPEN** (serial AF ~15 min wall ≠ free-list soak) |
| I3t hold-prior | temporary KEEP (`RetainedPrior` not Completed) |
| merge_green | **false** |

`merge_green` requires PASS ∧ west COVERED ∧ absolute ∧ policy ∧ operator — not claimed.

### Follow-on 2026-09-17: RD fog thrash + rim FirstMesh (P0/P1)

Systemic RD ownership (Memory Green keep-only; Apply shrink-only; Fog passthrough
when `!hole_debt`). P1 prune keep=focus under rim debt **regressed** manual
[`perf_20260917-153347_36940.jsonl`](bin/logs/perf_20260917-153347_36940.jsonl)
(wall med 31→78, side-black, sea clnm max 23).

### Rim regress repair (same day)

Code: Keep freeze when Adaptive &lt; altitude base (Evaluate + **per-frame Apply**);
Adaptive expand `dirty&lt;48` / PhysMs&lt;28 (shrink stays `dirty&gt;64`); prune
`rim_debt` **reverted**; RimIngress nh≤4 floor kept. Shrink@96 / probe-expand
**rejected** after AF v1 (wall blew to ~230).

| Signal | Result |
|---|---|
| unit demoted keep freeze + prune min(lit,focus) | PASS (`miss_first_mesh_class_test`) |
| AF cold/warm adequacy | PASS (exit 2 dual-lane OPEN ok) |
| AF warm mid wall vs regress 151235 mid ~133 | **~111** (better; strict plan ≤82 **not met**) |
| AF warm emerge med | **~20** (≤22 gate) |
| AF warm clnm max | **12** (≤10 gate **narrow miss**; cold max still **31**) |
| keep_cols=225 while demoted | ~1 period / flight (freeze holds) |
| fog thrash (manual) | **UNTESTED** post-repair (AF forces fog off) |
| operator west sea rim | **UNTESTED** |
| dual-lane mid stalled | still **OPEN** (~54–60) |

Verdict: repair **partial** — wall directionally better than P0/P1 regress AF;
plan absolute wall/clnm gates and manual sea rim still **OPEN**. Not merge_green.

### Follow-on 2026-09-17: Miss Ownership SLA (post manual 182042)

Coverage lag + sticky water miss after rim repair. Code: pin-until-drawable
(overrides hard-expire hop); HoleDrain exit-guard under coverage sticky;
PreferKick/steal Remesh→FM under sticky; MaxOutside≥1 when miss/SoftDeferEmpty;
telem `miss_owner_*`.

| Signal | Result |
|---|---|
| unit pin-until-drawable + HoleDrain sticky + PreferKick/steal | PASS (`miss_first_mesh_class_test`) |
| AF cold adequacy / west COVERED | PASS / COVERED (`miss_own_sla_cold`) |
| AF warm adequacy / eye-proxy | PASS / PASS (`miss_own_sla_warm`) |
| AF dual-lane mid stalled | **OPEN** (cold ~62.5 / warm ~54) — accepted separate epic |
| AF cold wall med / clnm max | ~78 / **46** (late-half med 18; drains to 0) |
| AF warm wall med / clnm max | ~86 / **36** (late10 drains) |
| AF HoleDrain share | cold mode3≈166/200; warm mode3≈200 — exit-guard holds |
| miss_owner_hop_n (AF) | end ~18 (telem live) |
| manual west sea fog-ON vs 182042 | **UNTESTED** (operator required) |

Verdict: SLA **landed + AF adequacy PASS**; clnm spike / sticky miss / dual-lane
still **OPEN**. Manual sea rim remains gate for CLOSED.

### Follow-on 2026-09-17: MissOwn VB regress repair (post manual 204032)

Manual [`perf_20260917-204032_17972.jsonl`](bin/logs/perf_20260917-204032_17972.jsonl):
clnm fixed vs 182042 but late VB~198, mode3×143, hop→31, wall~118.
Code: HoleDrain sticky = clnm|SDE|PLRNR only (not bare miss); remesh protect
under VB stall even in consume_mode; Site A+B pin-until-drawable + flicker damp;
aged-pin PreferKick rate-limit 12f; rim_mesh_debt without bare FocusMissing.

| Signal | Result |
|---|---|
| unit P0–P3 predicates | PASS (`miss_first_mesh_class_test`) |
| AF cold adequacy / west / eye | PASS / COVERED / PASS (`missown_vb_fix_cold`) |
| AF warm adequacy / west / eye | PASS / COVERED / PASS (`missown_vb_fix_warm`) |
| AF dual-lane | **OPEN** (cold mid stalled ~57 / warm ~53.5) |
| AF cold wall med | **~73** (better vs 204032 ~118; vs miss_own_sla ~78) |
| AF warm wall med | **~100** |
| AF cold clnm max / late max | 34 / 34 (warm late max **7** — anti-182042 clnm OK) |
| AF late VB | still high (~206) — operator gate |
| miss_owner_hop end | cold ~27 / warm ~47 — hop damp partial |
| manual fog-ON vs 204032 | **UNTESTED** |

Verdict: repair **partial** — wall/clnm directionally better on AF; late VB + hop +
mode3 share still OPEN. Operator west sea fog-ON required. Not merge_green.

### Follow-on 2026-09-18: Rim/ahead mesh converge (post manual 103803)

Manual [`perf_20260918-103803_56880.jsonl`](bin/logs/perf_20260918-103803_56880.jsonl)
on MissOwn VB [`26f5f035`](26f5f035): brief ahead-ring black + mid FPS dip.
Code: `ShouldDripOutsideFocusMeshOnRimCruise` (rim_hole|prefetch; not bare dirty_fm /
FocusMissing) under HoleDrain MaxOutside override; `ShouldDeferPrefetchAheadForFmStarve`
hard-starve (`schedule_ok==0`); cruise `NearLoad` ceiling `min(RD, max(focus,lit)+2)`
instead of `-1`; sticky exits unchanged.

AF calibrate: v1 soft `<floor` Prefetch defer discarded (stream_loads=0, west UNTESTED);
v2 hard-starve + ceiling+2; v3 drop dirty_fm-only drip.

| Signal | Result |
|---|---|
| unit P0–P3 predicates | PASS (`miss_first_mesh_class_test`) |
| AF cold adequacy / west | PASS / COVERED (`rim_ahead_converge_v3_cold`) |
| AF warm adequacy / west / eye | PASS / COVERED / PASS (`rim_ahead_converge_v3_warm`) |
| AF cold eye-proxy | **FAIL** (stale_visual Δ med=2) — honest OPEN |
| AF dual-lane | **OPEN** (cold stalled ~54 / warm ~61) |
| AF cold wall med / mid wall | **~78** / ~126 (vs missown_vb cold ~74 / ~94) |
| AF warm wall med / mid wall | **~91** / ~219 (streamer mid still elevated) |
| AF cold clnm max / late med | 25 / 14 (≪182042 class 33; warm late **0**) |
| manual fog-ON vs 103803 | **UNTESTED** (operator required) |

Verdict: epic **landed + AF adequacy PASS**; mid streamer diet partial on AF;
ahead-black / fog thrash remain operator gates. AF ≠ CLOSED. Not merge_green.

### Follow-on 2026-09-18: FullyDark FM fairness (post manual 152744)

Manual [`perf_20260918-152744_59252.jsonl`](bin/logs/perf_20260918-152744_59252.jsonl):
ticketed FullyDark / stale VL (`ok_fm=0`, `skip_snapshot~68`, stalled~55) while rim-ahead
streamer KEEP. Code: `ShouldStopRemeshSnapshotForFmResidual` (frac **0.65** after AF
calibrate); `ShouldYieldRemeshSlotToFmUnderProtect`; PreferKick-over-Dirty on
ticketed FullyDark with PendingGpu/RAA (no N04 census Dirty).

AF: v1 frac0.5 discarded (mid wall~181); **v2** frac0.65 gate-of-record.

| Signal | Result |
|---|---|
| unit P0–P2 predicates | PASS (`miss_first_mesh_class_test`) |
| AF cold adequacy / west / eye | PASS / COVERED / PASS (`fd_fm_fair_v2_cold`) |
| AF warm adequacy / west / eye | PASS / COVERED / PASS (`fd_fm_fair_v2_warm`) |
| AF dual-lane | **OPEN** (cold stalled ~62 / warm ~56) |
| mid `ok_fm` / `skip_snapshot` | **1** / **0** (vs manual 0 / ~68) |
| AF cold wall med / mid | **~88** / **~84** |
| AF warm wall med / mid | **~82** / **~82** |
| manual fog-ON vs 152744 | **UNTESTED** (operator required) |

Verdict: FM snapshot fairness **landed**; ticketed FullyDark image convergence still
OPEN (stalled~56–66). AF ≠ CLOSED. Not merge_green.

### Follow-on 2026-09-18: Prior-lit hold + ring clamp (post manual 183457)

Manual [`perf_20260918-183457_51304.jsonl`](bin/logs/perf_20260918-183457_51304.jsonl):
FPS ok; left-of-course black = LitDrawable FullyDark / rim miss (`dark_face_cz=4`).
Code: `ShouldRetainPriorLitOverUnlitCandidate` + hide first FD in ring; SoftDefer empty
avoid over lit; `ShouldClampIngressForLitConvergenceDebt` (hard FM starve / relight BP)
clamps NearLoad, sheds Prefetch lateral, blocks Adaptive expand, drip only on rim_hole
when `schedule_ok==0`. No N04 census Dirty.

AF: v1 soft under-floor clamp discarded (eye staleΔ); **v2** hard+BP gate-of-record.

| Signal | Result |
|---|---|
| unit prior-lit + ingress predicates | PASS |
| AF cold adequacy / west / eye | PASS / COVERED / PASS (`prior_lit_ring_v2_cold`) |
| AF warm adequacy / west / eye | PASS / COVERED / PASS (`prior_lit_ring_v2_warm`) |
| AF dual-lane | **OPEN** (cold stalled ~64 / warm ~61) |
| commits | `c340efc2` prior-lit hold; `b17cc0dd` ring clamp |
| manual fog-ON left-black vs 183457 | **UNTESTED** (operator required) |

Verdict: zero-in-frame contract **landed on AF**; operator left-black gate OPEN.
AF ≠ CLOSED. Not merge_green.

## KEEP

N01 incomplete=0, LegalDark rollback, frustum N02, cooldown N06,
I3t hold-prior (temporary).

## FREEZE

N04 census FullyDark remesh caps / wrong-tex gates on misnamed `pass_mdi_stale_*`
(compat alias only; stop-lines use `pass_packed_without_mdi_resident_n`).

## Evidence pointers

- B0: `audit16_gap_b0_cold` / `perf_20260917-104913_9248.jsonl`
- R1b: `audit16_gap_r1b_cold` / `perf_20260917-110204_11552.jsonl`
- S5: `audit16_gap_s5_cold` / `perf_20260917-110838_6128.jsonl`
- S2: `audit16_gap_s2_cold` / `perf_20260917-112534_33872.jsonl`
- S4: `audit16_gap_s4_cold` / `perf_20260917-113029_31984.jsonl`
- S6–S8: `audit16_gap_s68_cold` / `perf_20260917-113520_43140.jsonl`
- S9: `audit16_gap_s9_c{1,2,3}_cold` + `audit16_gap_s9_w{1,2,3}_warm`
- P0/P1 RD+rim: `rd_fog_rim_p0p1_cold` / `perf_20260917-151235_28788.jsonl`
  (anchor fail class: manual `perf_20260917-120407_19380.jsonl`)
- P0/P1 **regress** manual: `perf_20260917-153347_36940.jsonl`
- Rim regress repair AF: `rim_regress_fix_v3_{cold,warm}` /
  `perf_20260917-175628_21412.jsonl` / `perf_20260917-175915_32016.jsonl`
  (v1/v2 AF discarded — shrink@96 / missing per-frame keep freeze)
- Manual post-repair lag/sticky: `perf_20260917-182042_43968.jsonl`
- Miss Ownership SLA AF: `miss_own_sla_{cold,warm}` /
  `perf_20260917-200256_35528.jsonl` / `perf_20260917-200602_3496.jsonl`
- Manual MissOwn VB regress: `perf_20260917-204032_17972.jsonl`
- MissOwn VB repair AF: `missown_vb_fix_{cold,warm}` /
  `perf_20260917-220441_11912.jsonl` / `perf_20260917-220725_40068.jsonl`
- Manual post-MissOwn rim/ahead: `perf_20260918-103803_56880.jsonl`
- Rim ahead converge AF (gate of record **v3**): `rim_ahead_converge_v3_{cold,warm}` /
  `perf_20260918-140920_57732.jsonl` / `perf_20260918-141223_59232.jsonl`
  (v1/v2 discarded — soft Prefetch defer / dirty_fm drip)
- Manual post-rim FullyDark unlit: `perf_20260918-152744_59252.jsonl`
- FullyDark FM fairness AF (**v2**): `fd_fm_fair_v2_{cold,warm}` /
  `perf_20260918-174932_43608.jsonl` / `perf_20260918-175226_63736.jsonl`
  (v1 frac0.5 discarded — mid wall~181)
- Manual post-FM left FullyDark: `perf_20260918-183457_51304.jsonl`
- Prior-lit hold + ring clamp AF (**v2**): `prior_lit_ring_v2_{cold,warm}` /
  `perf_20260918-200939_43228.jsonl` / `perf_20260918-201222_47336.jsonl`
  (v1 soft under-floor discarded — eye staleΔ)

## Remaining blockers for true CLOSED

1. Manual `operator_visual=PASS` on west mid **and** sea rim vs **183457**
   (fog thrash=0; left LitDrawable black=0; wall mid class; hop KEEP).
2. Wall-clock continuous soak 10–15 min (free-list/dirty/age non-linear).
3. Dual-lane mid stalled ≤5 — **OPEN-with-cause** for merge (mid FullyDark stall ~61–65 after prior-lit/ring; separate epic; do not reopen N04 remesh caps).
4. Optional: drop I3t after more Replace field soak (**KEEP** until then).
5. Stop-segment rim plateau ≤2 (follow-on after R1; acceptance after operator).
6. Ticketed FullyDark census/stalled still elevated on AF despite hide/hold — image gate is operator left-black.
7. AF cold eye-proxy stale_visual Δ (rim_ahead v3) — follow-up, not greenwash.

### Follow-on 2026-09-18: S1 ownership roadmap (S0-lite)

| Signal | Result |
|---|---|
| `publication_audit` R01 retain / Live∩Free | PASS (`correctness_violations=0`; CURRENT retain @`GreedyGpuPublication` ~432) |
| AF cold adequacy / west / eye | PASS / COVERED / PASS (`s1_p0_own_cold`; exit 2 dual-lane OPEN ok) |
| S0 eye-proxy **negative** controls (null→PASS guards) | **FOLLOW-UP** (not blocking this roadmap; GateRepro / scorecard note) |
| continuous free-list soak 10–15 min | **UNTESTED** (same as blocker #2) |
| generation Free ledger (allocationId+generation) | PASS (`28dfc64f`; `generation_ledger_rejects_stale_free=1`) |
| AF cold/warm after generation | PASS / COVERED / PASS (`s1_p1_gen_{cold,warm}`; dual-lane OPEN) |
| S1 absolute gate live/free overlap | **CLOSED** for this roadmap (full S2 typed Replace still OPEN) |

### Follow-on 2026-09-18: LitDrawable relight drain (no N04)

| Signal | Result |
|---|---|
| Apply floor PL≥16 cruise | PASS (`8d5c0df3`; AF `lit_drain_p0_cold` hard PASS; wall fly ~65) |
| PreferKick + promote_relight under HoleDrain | PASS (`lit_drain_p1_{cold,warm}` hard PASS; dual-lane OPEN) |
| N04 census remesh Dirty | **KEEP FREEZE** (not reopened) |
| mid vs manual 204102 clnm/unlit | follow in operator / later matrix note |

### Follow-on 2026-09-18: underwater water walls (R06)

| Signal | Result |
|---|---|
| fluid hide via shellBlocks under overlay AIR | PASS (`a68e04b4`; `FluidMeshFacesTest` water\|water !drawable → 0 walls) |
| AF `water_seam_p0_cold` | PASS / COVERED / PASS |
| sea-band seam remesh on first drawable | PASS (`water_seam_p1_{cold,warm}` hard PASS; sea-cy gate) |
| operator underwater dive walls=0 | **UNTESTED** (R10 SoT; AF ≠ pixels) |

### Follow-on 2026-09-18: R09 frame GPU timers (S3 telem slice)

| Signal | Result |
|---|---|
| per-frame kick/finish reset ownership | PASS (`7554fa58`; reset in `ConsumeGpuApplyBacklog`; Rebuild preserves when `skip_gpu_consume`) |
| AF `r09_timer_p0_cold` | PASS / COVERED / PASS (dual-lane OPEN ok); wall fly med ~50; mesh_gpu_kick/finish med tiny (not multi-second accumulate) |
| aggregation note | kick/finish/async_drain are **per-frame Consume ownership**; do not sum across Rebuild skip frames or treat last-frame ms as wall-stage proof (audit R09) |
| geometric oracle / eye-proxy fail-closed | **OUT OF SCOPE** this epic (S3§4–7) |

### Follow-on 2026-09-18: streamer diet (R08-lite)

| Signal | Result |
|---|---|
| MaxLoadOps cap under lit-convergence debt | PASS (`2d6eeadc`; no fly boost + cap=4 after frontier floor; underfeet KEEP) |
| AF `stream_diet_p0_{cold,warm}` | PASS / COVERED / PASS; wall fly ~66–72 (class ≤~70–80 KEEP vs soft ≫90) |
| one HoleDrain→Warm emergency carve-out ledger | PASS (`ArmHoleDrainEmergencyCarveOutFrames` + `TryApplyHoleDrainWarmCarveOut`) |
| AF `stream_diet_p1_{cold,warm}` | PASS / COVERED / PASS (cold eye flake once then PASS; dual-lane OPEN) |
| full R08 unified spend ledger | **OUT OF SCOPE** (explicit; one carve-out only) |
| roadmap E1–E5 absolute for this cut | **CLOSED** for coded gates; operator dive / soak / dual-lane still OPEN blockers |

### Follow-on 2026-09-19: wrong-tex S2 residual (manual 100828)

| Signal | Result |
|---|---|
| `pass_dual_backend_same_coord_n` (honest R03) | PASS med/max **0/0** on `wrong_tex_p0/p1/p2` AF |
| packed exclusion when `opaque_draw` empty | PASS (`894f633f`; MDI-resident skip) |
| RepresentationSwitch before packed resident | PASS (`774d2132`; early bind + RemoveCoord order) |
| AF `wrong_tex_p0/p1/p2` | PASS / COVERED / PASS; wall fly ~23–25 (≤100828 class); fog thrash=0; incomplete/oom=0 |
| operator west wrong-tex blink=0 | **UNTESTED** (R10 SoT; coded dual-draw CLOSED) |
| full S2 + geometric oracle | **OPEN** follow-up (audit) |

### Follow-on 2026-09-19: regress after manual 111235

Anchor: `bin/logs/perf_20260919-111235_15716.jsonl` (FPS↑ wall~19.9; tex swap late; empty/black blink; walls residual).

| Signal | Result |
|---|---|
| E0 R06 remesh coalesce (no 3×3 / deep Y) | PASS (`8fb745fe`; AF `regress_e0_remesh_*` hard PASS; dirty_remesh max↓ vs prior AF) |
| E1 material blockId-flip attribution | PASS (`dc28ef03`; `publication_material_block_id_flip_n`) |
| E1 I3t visual prior (empty spoof) | PASS (`e212df2f`; AF `regress_tex_p1_*` hard PASS after cold flake) |
| AF `regress_tex_p2_cold` KEEP | PASS / COVERED / PASS |
| operator west wrong-tex blink=0 | **UNTESTED** (R10; coded path CLOSED, dual≡0 + flip mid≡0 on AF) |
| operator dive walls=0 | **UNTESTED** (follow E2) |

### Follow-on 2026-09-19: E2 walls + E3 matrix vs 111235

| Gate | Status | Cause / note |
|---|---|---|
| E0 remesh coalesce / empty blink coded | **CLOSED** coded | `8fb745fe`; operator empty blink **UNTESTED** |
| E1 wrong-tex load race coded | **CLOSED** coded | flip counter + I3t visual prior; operator tex **UNTESTED** |
| E2 peer sea-band same-cy remesh | PASS (`fix` after `8fb745fe`; AF `regress_walls_p0` hard PASS after eye flake) |
| operator dive walls=0 | **UNTESTED** (R10) |
| dual-lane / mid FullyDark stalled | **OPEN** | AF exit 2 OK |
| S2 full / S3 oracle / soak / R08 | **OPEN** | audit debt |
| R01 incomplete·oom / fog / MissOwn / N04 / ADR stamp | **KEEP** | no reopen |
| merge_green | **false** | operator UNTESTED + dual-lane OPEN |
| 111235 vs plan 100828 | product gates not fully CLOSED; FPS KEEP; remesh flood addressed |

### Follow-on 2026-09-19: underwater water walls after 100828 (R06 field)

Anchor manual: `bin/logs/perf_20260919-100828_41648.jsonl` (dive y≈47 walls FAIL R10).  
ADR stamp KEEP (drawable ∉ `InputsStillValid`).

| Signal | Result |
|---|---|
| subsea remesh when underwater/near fluid | PASS (`db007ca3`; `SeaSeamRemeshPolicy` band 2→4, deeper Y; stamp unchanged) |
| AF `water_walls_p0_cold` | PASS / COVERED / PASS; wall fly ~23.2; fog≡4; dual_backend mid 0; incomplete/oom≡0; dual-lane OPEN |
| overlay hide residual + deep/waterlogged units | PASS (`b5fead65`; `CellHasRenderableFluid` overlay-aware; `FluidMeshFacesTest`) |
| AF `water_walls_p1_{cold,warm}` | PASS / COVERED / PASS (cold eye flake once then PASS); wall fly ~23–24 |
| AF `water_walls_p2_cold` KEEP | PASS / COVERED / PASS; wall fly ~24.8; fog thrash=0 |
| operator dive walls=0 | **UNTESTED** (R10 SoT; AF ≠ pixels; coded remesh+hide CLOSED) |
| N04 FullyDark remesh Dirty | **KEEP FREEZE** |

### Follow-on 2026-09-19: AUDIT16 matrix cut vs 100828 (E3)

| Gate | Status | Cause / note |
|---|---|---|
| R01 generation Free / incomplete·oom | **KEEP** | 100828 + AF ≡0 |
| R02/R03 dual-backend same-coord | **CLOSED** coded | `pass_dual_backend_same_coord_n` mid≡0; operator west tex **UNTESTED** |
| R06 overlay water walls | **OPEN** prevent-emit coded | `fda799c6`; operator distant walls=0 UNTESTED |
| R04 pubver_without_fresh | soft only | AF max≤2 early; not oracle |
| dual-lane / mid FullyDark stalled | **OPEN** | AF exit 2 OK; not merge-green |
| S2 full writers+generations / S3 geometric oracle | **OPEN** | audit debt; out of this cut |
| soak / R08 full ledger / S5 demand | **OPEN** | audit debt |
| fog thrash / MissOwn / prior-lit / ring / N04 / ADR stamp | **KEEP** | no reopen |
| merge_green | **false** | operator UNTESTED + dual-lane OPEN |

### Follow-on 2026-09-19: R06 remesh Dirty bisect (manual 161124)

Start HEAD: `60202710`. Black-clean anchor (pre remesh-on-coverage risk): `27beca1c`.
Manual SoT: `bin/logs/perf_20260919-161124_66972.jsonl` (tex OK: dual_backend≡0 / block_id_flip≡0; blacks + water/black walls remain).

| Signal | 161124 fact |
|---|---|
| `softdefer_held_n` / empty_* | **≡0** — SoftDefer erase **not** the active black mechanism |
| `dirty_remesh_n` | med~66 max~86 |
| VB focus / FullyDark stalled | med~55 max~206 / med~46 max~166 |
| Hypothesis | first-drawable sea remesh (`d4e2f085`+) -> Dirty remesh backlog -> FullyDark stalled blacks; hide KEEP; SoftDefer-for-holes ban KEEP |
| R06 overlay water walls | **OPEN** coded partial (see results below) |

Track: R1 no-op remesh callback -> AF; R2 overlay-only remesh (peer `BoundaryOverlay.active`); R3 matrix honesty.


### Follow-on 2026-09-19: R06 remesh Dirty bisect results

| Step | SHA | AF | Mid VB med (vs 161124 87.5) | Mid FD stalled med (vs 60.5) | Note |
|---|---|---|---|---|---|
| R1 no-op remesh | `54ddfd83` | cold exit 2; adequacy+eye PASS | **62** | **44** | confirms remesh→Dirty/blacks; SoftDeferHeld≡0 |
| R2 overlay-only | `6a07c8d7` | warm exit 2; adequacy+eye PASS (cold eye flake once) | **76** | **51** | peer remesh iff `BoundaryOverlay` face bit; dual/flip≡0; fog≡4 |

| Gate | Status | Cause / note |
|---|---|---|
| R06 overlay water walls | **OPEN** coded partial | overlay-only remesh landed; operator dive **UNTESTED**; AF ≠ pixels |
| remesh-on-coverage flood | **mitigated** | R1 proved; R2 narrower than pre-bisect; VB/FD still better than 161124, not as low as R1 no-op |
| SoftDefer-for-holes / SoftDeferHeld | **KEEP** | held≡0 on SoT; not the black mechanism |
| dual-lane / mid FullyDark stalled | **OPEN** | AF exit 2 OK |
| merge_green | **false** | operator UNTESTED + dual-lane OPEN |


### Follow-on 2026-09-19: ring fog + zero distant walls (SoT 170548)

Manual SoT: `bin/logs/perf_20260919-170548_64892.jsonl` (post R06 overlay-only remesh).

| Mid west | 161124 | 170548 |
|---|---|---|
| VB focus med | 87.5 | **79** |
| FD stalled med | 60.5 | **46** |
| unfinished med/max | 2 / 9 | **12 / 22** |
| fog_pull_in_rd | 4 | **4** (no thrash; no pull on unfinished) |
| dual / flip | 0 | **0** KEEP |

| Gap | Cause | Track |
|---|---|---|
| Brief ring blacks | fog latch only near-miss; unfinished/VB do not pull fog | W1 fog hole_debt |
| Distant underwater water walls | overlay GetNeighborLoadState=Air forces solid emit; remesh one-shot | W2 prevent-emit (DoD: **zero distant walls**, not approach-heal-first) |
| Approach-heal | safety net only (W2b) | after prevent-emit |

Operator: dive distant walls=0; rim flash masked by fog. merge_green false until operator.


### Follow-on 2026-09-19: ring fog + walls results

| Step | SHA | Result |
|---|---|---|
| W0 SoT 170548 | `8617d8aa` | docs |
| W1 fog unfinished/VB latch + `prior_lit_hold_n` JSONL | `fda799c6` | coded; product-174657 AF forces fog OFF so fog med not AF-gated; unit PASS |
| W2 prevent-emit overlay→Unknown + fluid hide + coalesce xz,cy | `fda799c6` | unit solid/fluid walls=0; InputsStillValid Unknown |
| W2b approach-heal sticky overlay | `fda799c6` | safety net cap≤4 |

AF cold `ring_fog_w1w2`: eye PASS / west COVERED; dual/flip≡0; mid VB **34.5** (was 79 on 170548) — adequacy FAIL `vb_too_low_for_product_class` (inverted: lower VB is the win). Warm similar exit 2.

| Gate | Status |
|---|---|
| Ring fog mask (manual fog ON) | **coded** — operator rim flash UNTESTED |
| Distant underwater walls | **coded prevent-emit** — operator dive distant=0 **UNTESTED** |
| R06 walls | **OPEN** until operator dive |
| merge_green | **false** |


### Follow-on 2026-09-19: SoT 185830 hang + walls + fog-off

Manual: `bin/logs/perf_20260919-185830_34388.jsonl` (post prevent-emit `fda799c6`).

| Symptom | Evidence | Root |
|---|---|---|
| Shore FPS freeze | periods 26-28: wall 5497/231/952 ms; `streamer_update_ms` ~5.5-6.4s; `mesh_emerge` ~3ms | sync EnsureChunkLoaded / full-column gen without FrameDeadline preempt; MarkDirty on complete |
| Distant water walls | eye FAIL | sticky baked quads; remesh peers too narrow |
| Rim black | VB mid 45 (better than 170548 79); `fog_hole_debt`≡0 | `bin/config.json` `fog_pull_in_enabled=false` — W1 latch dead; AF can leave fog OFF |

Note: `dirty_dropped`≈14630 is cumulative counter (not per-period spike alone).

Track: H1 streamer deadline → W1 sticky overlay remesh → F1 fog ON + AF restore hygiene.


### Follow-on 2026-09-19: H1/W1/F1 results (SoT 185830)

| Step | SHA | Result |
|---|---|---|
| H0 SoT evidence | `ba09c573` | docs |
| H1 StreamerUpdate deadline + no sync 256 + defer Dirty | `0eb9ea3e` | unit PASS; AF cold eye PASS / west COVERED |
| W1 sticky overlay peer remesh + Apply BoundaryOverlay | `3a1450a4` | unit PASS; fluid walls=0 KEEP |
| F1 fog ON + AF restore hygiene | `187f88ce` | operator config fog true; AF restore True (preset=quality) |

AF cold `s185830_h1w1f1_af_cold` (`perf_20260919-192746_18136.jsonl`): exit 2 dual-lane OPEN OK; eye PASS; west COVERED; wall med ~26 ms; `streamer_update_ms` med ~4.4 ms but **max ~6.5 s** still on one period (cx=-12) — hang **mitigated med / OPEN max** (stop-line >1s → Frontier async-only follow-up, not landed this track). dual/flip≡0. Fog after AF: **true**.

| Gate | Status |
|---|---|
| Shore hang (manual) | **OPEN** — H1 coded; AF med OK; AF max streamer still multi-second; shore retest UNTESTED |
| Distant walls | **coded** sticky remesh — operator dive distant=0 **UNTESTED** |
| Rim fog latch | **coded** fog ON + AF restore — operator rim flash UNTESTED |
| merge_green | **false** |


### Follow-on 2026-09-19: H1/W1 regress rollback

Operator after H1+W1: blacks↑, empty/flicker in flight, walls remain, 0-FPS hang unchanged.

| Cause | Action |
|---|---|
| W1 broaden sticky remesh | **reverted** to R2 face-toward + sea-band (remesh→FullyDark class) |
| H1 prefer-async fallthrough | **reverted** — left empty columns |
| H1 defer MarkDirty drain≤8 | **reverted** — empty/flicker while Exhausted |
| H1 FrameDeadline load break + sync budget 32 | **KEEP** |
| Apply BoundaryOverlay stamp + F1 fog restore | **KEEP** |
| prevent-emit Unknown | **KEEP** |

Hang max still OPEN (Frontier follow-up). Walls OPEN (need non-Dirty heal). merge_green false.

AF cold after rollback (`perf_20260919-201718_28016`): eye PASS / west COVERED; `streamer_update_ms` med 2.6 / **max 74** (was max ~6.5s with prefer-async); stale_visual mid **1** (was 7); fog restore True. dual-lane OPEN ok.


### Follow-on 2026-09-19: stop unload + keep-shell under FrameDeadline (SoT 210431)

Manual SoT: `bin/logs/perf_20260919-210431_66120.jsonl` (post H1/W1 rollback `73af7321`).

| Fact | Value |
|---|---|
| Hang | periods 20–23, `spd=0`, `cx=-4`, `y=48–54`; `max_wall` 0.7–1.5s |
| Loads | `stream_loads=0`, `stream_load_candidates=0` |
| Telem lie | `streamer_update_ms` was full `UpdateStreaming` wall (WorldViewBinding overwrite) |
| Vs 185830 | max wall ~5.5s → ~1.5s (budget 32 helped); stop hang still OPEN |

**Root (T0 AF dive):** hang = `UnloadDistantChunks` ForEach+sync save on stop under water; keep-shell secondary. PrefetchAhead already no-op on stop.

#### AF-DIVE harness

| Item | Path |
|---|---|
| Scenario | `product-174657-dive` (`tools/flight_sim_run.py`) — west + `--dive-phase` + underwater stop |
| Analyze | `tools/analyze_stop_hang_dive.py` |
| Bake-off driver | `tools/bakeoff_stop_hang_dive.py` |
| Baseline | `bin/suite_reports/stop_hang_dive/stop_hang_baseline.json` (U0K0; unload_max **4201.9** ms) |

Coverage fail / no underwater stop → `UNTESTED` (not PASS).

#### Unload bake-off (keep=K0)

| Mode | Algorithm | Best score (×2 cold) | stop wall max | unload max | note |
|---|---|---|---|---|---|
| U0 | baseline telem | 1432 / 1453 | 109 / 180 | ≈0 | hang intermittent vs baseline spike |
| **U-A (1)** | Exhausted early-out | **1338** / 1381 | **77** / 156 | ≈0 | **winner** |
| U-B (2) | + scan cursor | 1420 / 1611 | 143 / 302 | ≈0 | |
| U-C (3) | + tighter stop gate | 1358 / 1498 | 113 / 277 | ≈0 | |
| U-D (4) | + defer save | 1589 / 1602 | 237 / 313 | ≈0 | both FAIL AF wall target |

Default `UnloadAmortizeMode=1` (U-A). Losers remain in enum for regress.

Stop-line note: when hang reproduces, T0 proves unload dominates (`streamer_unload_ms`); on calm cold runs unload_max≈0 and wall is elsewhere (fluid_map class) — still keep U-A Exhausted gates.

#### Keep-shell bake-off (unload=U-A)

| Mode | Algorithm | Best score | stop wall | keep max | note |
|---|---|---|---|---|---|
| K0 | baseline | 1383 / 1465 | 122 / 148 | ≈0 | |
| K-A (1) | Exhausted / frame gate | 1343 / 1515 | 118 / 302 | ≈0 | |
| **K-B (2)** | + cheap filter shortlist | **1328** / 1456 | **123** / 235 | ≈0 | **winner** |
| K-C (3) | + annulus cursor | 1436 / 1463 | 134 / 231 | ≈0 | one unload spike 78 |
| K-D (4) | + disable idle UW | 1345 / 1422 | 140 / 209 | ≈0 | |

Default `KeepShellAmortizeMode=2` (K-B).

#### Code SHA / gates

| Step | SHA | Result |
|---|---|---|
| T0 telem + AF-DIVE harness + U/K modes | `1621440d` | unit `StreamerAmortizePolicyTest` PASS; dive coverage PASS; baseline unload spike honesty |
| U winner default U-A + K winner default K-B | (this commit) | bake-off tables above |

| Gate | Status |
|---|---|
| Stop unload hang (SoT class) | **mitigated coded** — Exhausted+amortize; AF dive wall often &lt;200 when hang does not fire; operator retest UNTESTED |
| Keep-shell stop cost | **mitigated coded** K-B |
| Walls underwater | **OPEN** (separate non-Dirty track) |
| dual-lane / merge_green | **OPEN** / **false** |

One-liners:

```text
python tools/flight_sim_run.py --scenario product-174657-dive --report bin/suite_reports/stop_hang_dive/manual.json
python tools/analyze_stop_hang_dive.py <perf.jsonl> --report <report.json> --out bin/suite_reports/stop_hang_dive/out.json
python tools/bakeoff_stop_hang_dive.py --phase u --keep-fixed 0 --repeats 2
python tools/bakeoff_stop_hang_dive.py --phase k --unload-fixed 1 --repeats 2
```

### Follow-on 2026-09-20: operator 090834 — hang win + tex/flicker OPEN

Manual: `bin/logs/perf_20260920-090834_65008.jsonl` (post U-A/K-B amortize).

| Signal | Fact | Verdict |
|---|---|---|
| Hang / FPS | `streamer_unload_ms`≈0; `max_wall` med~41 / max~179 | **KEEP win** (vs 210431 0.7–1.5s) |
| dual / flip | both ≡0 | counters **insufficient** for operator wrong-tex |
| Flicker proxy | `mesh_apply_stale_geom` 0→24; `accepted_refresh` med~8 | remesh churn on drawable |
| SoftDeferHeld | ≡0 | not the mechanism |

**Root:** `HasNearbyFluidSurface` from air opened underwater `subsea_band=4` sea-seam remesh → stale-geom refresh flicker; flip telem same-size-gated.

Track: E0 honesty → T1 air-near-fluid gate → T2 flip any blockId → T3 I3t overlay/fluid force prior.

#### Results (T1/T2/T3)

| Step | Result |
|---|---|
| T1 `SeaSeamEyeContext` AirNearFluid vs Underwater | unit `mesh_neighbor_policy_test` PASS; CEC first-drawable + W2b heal use classify |
| T2 flip any `blockId` change | coded; AF product flip max **1** (was always 0 under same-size gate) |
| T3 I3t overlay/fluid prior force | unit `miss_first_mesh_class_test` PASS |
| AF product cold `perf_20260920-092358_61512` | eye PASS / west COVERED; dual≡0; unload max~0; wall max~146; stale mid med **0.5** |
| AF dive cold `perf_20260920-092557_51556` | stop_uw wall **59**; unload≈0; coverage PASS; score 1660 (hang KEEP) |
| Operator tex / air-flicker | **coded** — retest UNTESTED |
| Walls distant / dual-lane / merge_green | **OPEN** / OPEN / **false** |

KEEP: unload/keep amortize, prevent-emit, no broad remesh, no SoftDefer-for-holes.

