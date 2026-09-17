# Audit 2026-09-16 S9 closure matrix

Source audit: commit `7a658525` / `docs/streaming/CURRENT_STATE_AUDIT_2026-09-16.md`.
Tails plan: H0–P5 on `cursor_audit2_impl`.
Full cutover: E0–E9 on `cursor_audit3_impl`.
Gap-closure: B0→R1→S5→S2→S4→S6–S8→S9 on `cursor_audit3_impl` (2026-09-17).

## Absolute gates (must be green)

| Gate | Owner step | Status |
|---|---|---|
| live/free allocation overlap = 0 | S1 `publication_audit` | code + CTest |
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

## Remaining blockers for true CLOSED

1. Manual `operator_visual=PASS` on west mid **and** sea rim (post rim-regress-repair).
2. Wall-clock continuous soak 10–15 min (free-list/dirty/age non-linear).
3. Dual-lane mid stalled ≤5 — **OPEN-with-cause** for merge (mid FullyDark stall ~53–57; separate epic; do not reopen N04 remesh caps). Optionally close later with a dedicated lane fix.
4. Optional: drop I3t after more Replace field soak (**KEEP** until then).
5. Stop-segment rim plateau ≤2 (follow-on after R1; acceptance after operator).
6. Rim regress repair: AF warm wall med still &gt;82 vs plan; cold clnm spikes; need manual fog-ON confirm thrash=0 + side-black gone.
