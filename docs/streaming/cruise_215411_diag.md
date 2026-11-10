# Cruise column generation diagnosis (`215411`)

Inland corridor −485/50→55 after RelightThenMesh industrial P0–P4
(`9daa5151`: telem, pin-hold, surface-finalize nh≤2, FirstMesh boost,
keep-until-bind). Baseline for comparison is the Era28 publication
rollback flight `195810` (`5edc709c`).

This is **measurement**, not a bypass list. Do not “fix” holes with Unlit
near, hide exceptions, Imm, prune `keep_h=2`, or `*FloorMs`.

Compare logs:

- `bin/logs/perf_20260817-195810_30216.jsonl` (SoT rollback, no P0–P4)
- `bin/logs/perf_20260817-215411_29916.jsonl` (this flight, P0–P4 binary)

Same audit filter as `195810`: `chunk_count ≥ 80` (includes period copies
of spike rows; 158 vs 153 cruise samples — apples-to-apples). Unique
`kind=spike` rows on `215411`: 135.

## SLA vs `streaming_cruise_sot.md`

| Metric | Target | **195810** | **215411** |
|--------|--------|------------|------------|
| wall med / p90 | ≤130 / 220 | 204 / 279 | **198 / 355** |
| miss frame% | ≤47 | 88.2 | **98.7** |
| visible_black med | ≤25 | 40 | **75** |
| opaque_draw med | not collapse | 543 | **894** |
| late cz≥55 opaque | ≥200 | 108 | **111** |
| schedule_ok med | ≥8 | 8 | **4** |
| dirty_fm med | — | 204 | **138** |
| pending_light med | — | 6 | **14** |
| skip med | — | 48 | **71** |

Mid-cruise draw is the P4 win (opaque 894, packed 252, pool ~14 MB).
Throughput did not clear the witness: miss 98.7%, pending never cools,
skip↑, schedule_ok↓. Late opaque still collapsed. Wall p90 **regressed**.

Corridor `215411`: focus (−485,50)→(−485,55), 158 cruise frames, miss 156.

## P0 bottleneck class

P0 named three hypotheses. `215411` facts:

| Hypothesis | Verdict |
|------------|---------|
| Apply starve (`relight_completed_n=0` ⇒ no MarkRelit) | **Rejected.** `relight_apply_n` med **1**, sum **177**, 42/158 apply=0. `relight_completed_n==0` still 118/158 — ring occupancy ≠ throughput (as P0 warned). |
| Hitch-finalize >600 ms | **Rejected.** drain max **329**, drain>600 = **0**, drain>200 = 4 (same count as `195810`). p90 drain **79** vs 61 — mild cost, not Era40 597-class. |
| Hop + Capture not on the miss column | **Confirmed.** `relight_witness_hold_n` med **0**, max **1**. `softdefer_witness_retarget` 2→**208** (worse than 192). Max consecutive same miss cx/cz = **2** frames. |

Capture is not idle: `relight_capture_finalize=1` on 139/158, `cap_h` med
**1**, span med **5**. Apply runs. The witness still hops, and pending on
the **miss column** never leaves.

## Two-phase bottleneck (updated)

`195810` was: Relight does not finish the sticky witness (−487,48) cy=0,
then FirstMesh 8 vs Dirty ~200, then GPU dump at cz=55.

`215411` is the same pipeline with different failure geometry.

### A. P1 hold never arms — miss is missing-mesh, not PendingLight

Hold policy (`ShouldHoldPinnedRelightWitness`):

```39:44:src/World/Streaming/RelightFifoPolicy.h
inline bool ShouldHoldPinnedRelightWitness(int pinned_horiz,
                                          bool pinned_still_pending)
{
  return pinned_still_pending && pinned_horiz >= 0 &&
         pinned_horiz <= kVisualStageNearFovHoriz;
}
```

`RefreshStreamingPressure` only increments `RelightWitnessHoldN` when the
**current nearest miss** is nh≤2 **and** `IsPendingLightBeforeMesh(miss_xz)`.
SoftDefer retarget uses the same gate on the **pin** column. PromoteRelight
hold (`SetPromoteRelightHold`) is therefore almost never true.

On `215411`:

- miss with `pending_light_n>0`: **154 / 156** (global counter, not the
  miss column)
- frames `pending_light_n≤2`: **2** (195810 had **59**)
- miss nh≤2: 90/134 unique spikes; hold Counter `{0: 81, 1: 9}`
- miss hops: top (−485,50)×29, (−486,55)×25, (−486,52)×18, (−484,54)×16…
  — not sticky (−487,48)×24
- miss_horiz: nh=1×78, nh=2×24, nh=0×19 (nearer than `195810`)
- miss_cy: **cy=2×78, cy=1×41, cy=0×27** vs `195810` cy=0×111

The nearest greedy-miss is often a **cy=2 missing slice** while light
debt sits on **other** columns (pending med 14). Hold requires pending
**on that miss key**. It never sticks, so Era27 hop (`better_horiz` /
pin_T) still fires (`softdefer_witness_retarget` 208).

FIFO still never empties: `relight_fifo_n` med **70** (min 54, max 76).
Dropped latches 0→30. `relight_drain_ms` med **32** on miss — Capture
runs; Completed ring stays empty because Y-band / requeue / hop, not
because apply is starved.

On nh≤2 miss spikes, Capture horiz **matches miss horiz class** on 76/90
frames, finalize=1 on 86/90, apply often 1–2. Work is in the right
**distance band**, not on a **stable column** until MarkRelit.

### B. Pending never cools → FirstMesh cannot start (P3 idle)

DesiredStage still owns the column: `pending_light` → `RelightThenMesh`
→ FirstMesh skip. skip med **71** (was 48). schedule_ok med **4** (was 8)
with `first_mesh_schedule_cap` still 6.

`BoostJustRelitNear` / `SetJustRelitFirstMeshColumn` only fire after
`MarkRelit` with `finalize_pending_gate`. While pending owns the miss
column (or DesiredStage stays RelightThenMesh because **some** pending
exists in the ring), P3 cannot move the hole.

dirty_fm med **138** (was 204) — the FM queue is smaller, but skip is
higher. This is not “admit 8 vs 200” anymore; it is **admit starved by
RelightThenMesh**.

`195810` cz=53 had `pl med=0` and miss 100% at nh=4–5. `215411` never
reaches that cool: pl med stays 11–33 on every cz step.

## Per-cz (`215411`)

| cz | miss | uf7 | pl med | fm med | skip med | opaque med | packed med | pool MB |
|----|------|-----|--------|--------|----------|------------|------------|---------|
| 50 | 47/47 | 0 | 33 | 148 | 61 | **915** | **253** | **14.2** |
| 51 | 21/21 | 0 | 17 | 197 | 65 | 894 | 252 | 14.5 |
| 52 | 15/15 | 0 | 11 | 150 | 96 | 897 | 252 | 15.7 |
| 53 | 11/11 | 0 | 13 | 137 | 81 | 896 | 252 | 16.4 |
| 54 | 9/9 | **8** | 11 | 119 | 89 | 894 | 249 | 16.4 |
| 55 | 53/55 | **55** | 12 | 98 | 74 | **111** | **29** | **2.32** |

Compare `195810` mid-cruise packed 77–150 / pool 10–14, then the same
cz=55 cliff. P4 keeps residency **until the last +Z step**.

## Opaque / underfeet (P4)

- Start: opaque 924, packed 257, pool **13.8 MB**
- Mid (cz=52): opaque **907**, packed **252**, pool **15.9 MB** (`195810`
  mid: 632 / 86 / 12.1)
- Stop: opaque **111**, packed **29**, pool **~2.3 MB**
- `underfeet_reason=7` (`NotReadyState`): **8/9** cz=54 frames (opaque
  still 894) **and all 55** cz=55 frames
- GpuInFlight (5): **32** frames (195810 had **2**) — more GPU in-flight
  underfeet, still not the late cliff
- `heal_deferred_for_miss=1` every cruise frame

cz=54 is still the tell: underfeet already NotReady while packed/pool
are healthy, then cz=55 residency collapses. Keep-until-bind
(`ShouldKeepGpuSlotUntilBindInRing` in Immediate/CommitGpu and
RecoverUnlit) holds slots **inside** the vis/keep ring. The last focus
step still frees without a BindCommitted replacement — same draw-SoT
dump as `195810`, delayed by one chunk of +Z.

NotLoaded(6) is gone. NotReadyState means emerge/visual ready, not
“chunk missing”.

## P2 surface-finalize

Policy: unsplit `finalize_gate=true` only for captured-column
`horiz_dist ≤ 2` (`kVisualStageNearFovHoriz`). Rim 3–4 keeps Y-split.

Facts:

- `capture_finalize=1` on **139/158**; `cap_h` med 1 (69 frames nh=1)
- drain p90 **79** vs gate ~61; wall p90 **355** vs 279
- no 600 ms class; first spike wall **1481** with `relight_drain_ms` 329
  (plus `fluid_map_cpu_ms` ~183 — enter hitch, not cruise Capture)
- **26** frames: `finalize=1` AND `cap_h≥3` (horiz 3×18, 4×6, 5×2)

Those rim-finalize frames are not `ShouldPreferMissFinalizeBand(3)`
(unit: nh=4 is false). `finalize_gate` defaults **true**; Y-split only
runs when `span > band_h`. Surface clamp can leave a span that already
fits one band, so rim still finalizes. That is a P2 leak, not enter-80 ms.

Miss geometry vs P2 intent: `195810` miss_cy≈0 (surface). `215411`
miss_cy **mostly 2**. Finalizing a surface band of a hopping nh=1 pending
column does not mesh the cy=2 witness.

## Witness / SoftDefer telem

- `softdefer_witness_retarget` 2 → **208** (P1 gate: retarget << 192 —
  **failed**)
- `softdefer_capture_floor_hits` 1 → **198**
- `softdefer_empty_placeholder_n` med **1** (195810 was 0)
- `pending_gpu_queued_n` med **10** (end 10); `mesh_dirty_gpu_n` med **1**
- `gpu_kick_n` / `gpu_finish_n` med **2**
- `relight_completed_discarded` = 0 (workers are not discarding results)
- `visible_black_focus_n` med **75** (was 40)
- `chunk_meshed_unlit_hidden` med **5**; `column_meshing` med **61**;
  `column_lighting` med **17**

GPU mesh apply is healthier mid-cruise (packed 252). Relight hop +
pending ownership + late keep-until-bind cliff are the industrial gaps.

## Root-cause statement

1. **P1 predicate is too narrow.** Hold requires the *nearest missing
   greedy* column to be `PendingLight`. On this flight the miss is often
   a cy=2 hole without pending on that key, while `pending_light_n` stays
   14 on *other* columns. FindNearest hops every 1–2 frames → hold_n max
   1, retarget 208, PromoteRelight hold never sticks.
2. **Apply is not the bottleneck.** 177 MarkRelit-class applies do not
   clear the witness; Capture often finalizes nh=1 pending that is not
   the sticky miss column. `relight_completed_n=0` remains a ring-occupancy
   lie.
3. **P3 cannot fire** while RelightThenMesh owns DesiredStage (skip 71,
   schedule_ok 4). dirty_fm 138 is not the rate limiter.
4. **P4 works until cz=55.** Mid-cruise packed/pool match a healthy
   `163559`-class residency. The last +Z step still dumps to ~2 MB /
   opaque ~111 / uf7×55. Keep-until-bind does not survive focus leaving
   the old underfeet column without a bound replacement.
5. **P2 hitch gate mostly held** (no >600 ms), but drain p90 and wall p90
   worsened; rim finalize leak (`cap_h≥3` + finalize=1) and span=5
   surface Captures on hopping nh=1 are the cost.

Not a publication-fork bug. Not “SoftDefer forgot to mesh.” Light-before-
visible-mesh is working; **pin-until-MarkRelit of the actual miss column
(even when it is missing-mesh rather than PendingLight)** and **late GPU
keep across the last focus step** are the remaining industrial gaps.

## Phase verdict vs gates

| Phase | Gate vs `195810` | `215411` |
|-------|------------------|----------|
| P0 telem | wall match, classify bottleneck | wall med OK; p90 worse; class = hop |
| P1 pin | retarget << 192; sticky pending clears | **fail** (retarget 208, hold max 1) |
| P2 finalize nh≤2 | drain p90 ≤61; no >600 ms | no >600; **p90 79**; rim leak 26 frames |
| P3 FM order | pl=0 + miss nh≤2 short-lived | **N/A** — pl never cools |
| P4 keep-bind | late opaque ≥200; pool not ~2 MB | **mid pass / late fail** (111 / 2.3 MB) |

## Heavy steps (wall) vs the miss goal

Raising Capture/FM **quotas** is still wrong (`StreamingPhaseBudgetMs=24`,
one Capture already **exceeds** it). Cheapening the **unit of work** is
the other lever besides pin.

Median cruise wall **198** ≈ streaming phase **119** + locomotion
**56** + render **19**. `phase_budget_over=1` on **146/158** frames;
`emerge_budget_ms` med **8** (`MissReservedMs`) because stream ate the
24 ms phase before TickMeshEmerge. Steady (cz≥51, wall<400): wall **186**,
drain **24**, emerge **44**, phys **55**, Imm **0**.

| Step | Med / p90 / max | What it is | Helps miss% if sped? |
|------|-----------------|------------|----------------------|
| Relight Capture 3×3×5 | 32 / 79 / 329 | `CollectColumnChunkCoords` copies **9 columns × span_cy** full `GetData`+light, then CPU `Compute` floods all of them (`grid=*this` copies again). P2 unsplits span=5. Tune `CaptureDrainMovingMs=3` is after the first Capture. | **Yes, if cheaper — not if more.** 1 job/frame ≈ FIFO 70 never drains. Cheap copy/flood → more MarkRelit/s without raising wall. Dropping 3×3 to 1×1 breaks 15-block light travel. |
| Locomotion substeps | 56 / 72 / 126 | `kMaxPhysicsSubsteps=12` × `1/60` after a 200 ms frame. `physics_block_ms` ≈0. | **Indirect.** Saves ~40 ms wall, less hot-skip. Does not Relight/mesh. |
| RebuildChunkImmediate | med 0; **61 frames**, those med **62** / p90 **73** | Underfeet Imm (`allow_uf_imm`) while `underfeet_reason=7` on 38/61. Budget checked *before* the 70 ms greedy. | **No — remove on cruise under pending**, do not speed. Dark bake vs RelightThenMesh. |
| mesh_emerge / Dirty+GPU | 59 / 119; gpu med **8.8** (195810 was **0**) | P4 keep → more GPU apply. Dirty tick ~15. | GPU is residency, not the hole. Do not drop keep to go faster. |
| Fluid map | med 0; spikes **183–200** | Start hitch with drain 329. `fluid_map_full_rebuild` 25 frames. | Not the steady cruise. |
| stream leftover | ~28 med | `StreamMs` minus Capture (UpdateStreaming / FIFO pin). | Secondary. |

`CaptureMovingBgCap=1` is forced by cost, not by desire: one 32 ms
memcpy already blows the 24 ms phase. `DynamicCaptureMovingBgCap` (up to
3 when `pending_light_focus>15`) cannot fire usefully until Capture is
cheap. Hop still wastes even a cheap Capture — pin remains first.

Radical Capture speed that is allowed: COW / shared chunk arrays (no
45×12 KB memcpy), skip the extra `Compute` snapshot copy, optionally
narrow witness Y to miss_cy±1 **after pin**. Not: worker-main Capture
(TD-ARCH-015 hung), enter `CaptureDrain=80`, Imm primary, Unlit near.

## Next (architecture only)

Do **not**: Unlit near, hole_fill_preview, prune `keep_h=2`, Imm, hitch
floors, raise Capture/`first_mesh_schedule` (wall p90 already worse).

Do:

1. Hold/pin the **miss column** while it is still `focus_missing_mesh`
   in nh≤2, even if that key is not `IsPendingLightBeforeMesh` (pending
   may live on a neighbor). Stop SoftDefer/Promote hop until that column
   has lit greedy at the miss cy — or until MarkRelit **of that column**.
2. Cheapen the Relight Capture **unit** (COW / no extra Compute copy) so
   one 3×3×5 job is a few ms, not 32. Then `bg_cap>1` can drain pending
   without blowing `StreamingPhaseBudgetMs=24`. Do not raise quotas on
   the current 32 ms memcpy. Cheap hop still misses the witness — pin
   first.
3. Capture+finalize must target that pinned key (not “any nh=1 pending”).
   Surface-band vs miss_cy=2: the missing slice must be in the Capture
   Y-range or FirstMesh must be allowed once that column’s pending
   clears.
4. Late keep-until-bind: cz=54 uf7 while packed still 249 is the same
   tell as `195810`. Do not FreeChunk the outgoing underfeet GPU until
   the incoming column is BindCommitted — without shrinking hinterland
   eviction or raising pool cap.

Re-run inland −485/50 with the same audit keys. Restore spawn
`(-7752, 96, 808)` yaw 90 pitch 0 after sessions.
