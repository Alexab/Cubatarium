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