# RelightThenMesh industrial path (cost-aware)

Inland cruise after Era28 publication rollback (`5edc709c`, flight
`195810`). This is the working SoT for **complete/apply Relight FIFO →
FirstMesh after light → keep-until-bind**. It is not a bypass list.

Evidence: [`cruise_column_gen_diag.md`](cruise_column_gen_diag.md)
(`195810` rollback). After P0–P4:
[`cruise_215411_diag.md`](cruise_215411_diag.md).
SLA: [`streaming_cruise_sot.md`](../streaming_cruise_sot.md).

Do **not**: Unlit near, `hole_fill_preview`, prune `keep_h=2`, Imm, new
`*FloorMs`, enter-style `CaptureDrain=80`, raise `first_mesh_schedule`
without P0 proof, Relight-steal-FirstMesh, FreeChunk-before-Bind.

## Why this order

`DeriveColumnDesiredStage` already owns the column:

- `pending_light` → `RelightThenMesh` (FirstMesh under pending **is** Unlit near)
- else `missing_visible` → `FirstMesh`
- draw publish: lit drawable **or** keep-prior GPU

Until Relight **finishes** the witness, FirstMesh and GPU quotas cannot
heal the hole. `195810`: skip med 48, 120/135 miss frames still
`pending_light_n>0`. SoftDefer is honest.

```
PendingLight ──(1 complete/apply)──► lit field
     └── FirstMesh skip (by design)

lit + missing ──(2 FirstMesh)──► CPU greedy / GPU upload
     └── hide FullyDark until lit bind

live GPU ──(3 keep-until-bind)──► draw until BindCommitted
     └── else sky hole on focus step
```

**Witness nh≤2** = nearest `focus_missing_mesh` with Chebyshev horiz≤2
(`IsNearFocusMissUrgent`). Sticky start on `195810`: (−487, 48) vs focus
(−485, 50). Rim nh=3–5 is a later queue, not the first complete target.

## Cost model (cruise, not enter)

Wall on `195810` is **already red**: med 204 / p90 279 vs SLA 130/220.
Any substep that buys miss% with more Capture/FM/GPU without proof is a
regress.

| Stage | Budget (tune) | `195810` fact | Role |
|-------|---------------|---------------|------|
| Capture `DrainRelightQueues` | holes moving 5–6 ms, `CaptureMovingBgCap=1` | drain med **30**, p90 **61**, max **597** | Main-thread 3×3 band copy. **Primary CPU risk.** |
| Y-band vs finalize | `RelightCaptureBandCy=4` → 3 when moving+holes | Era40 `ShouldPreferMissFinalizeBand(horiz≤4)` **unsplits** the drained column | Full surface Capture with `finalize_gate=true`. Underground already clamped by `RelightSurfaceBandForColumn`. Historical full-column ~1.6 s must not return. |
| Apply `DrainAsyncRelightResults` | 8–16/frame | `relight_completed_n=0` on 117/153 | Cheap vs Capture. Empty ring = apply **starves**, not the cap. `RelightCompletedN` is ring occupancy at sample, **not** throughput. |
| FirstMesh | HoleDrain schedule 6 + remesh steal → admit ~8 | `dirty_fm` ~200, `schedule_ok` 8 | Ring=4 cylinder, not hinterland. Raising slots raises `mesh_emerge` on a fat wall. |
| GPU keep | `GpuVertexPoolMaxMb=256` | pool 13.8→**2** MB, packed 258→31 | Memory, not CPU. Hinterland must still evict. |

**Bottleneck hypothesis (confirm in P0, do not treat with quotas):**
one Capture/frame (~30 ms) hits hop/hinterland (`softdefer_witness_retarget`
0→192); witness nh=2 never `MarkRelit`. Second risk: Era40 finalize nh≤4
inflates one Capture to hundreds of ms → hitch → hot-skip the next frames.

`DynamicCaptureMovingBgCap` already allows up to 3 Captures when
`pending_light_focus>15`. Do not raise `bg_cap` further.

## Witness vs two Relight contours

| Contour | Meaning | Code |
|---------|---------|------|
| Gate `PendingLightBeforeMesh` | column is not lit-ready; FirstMesh skip | `NotePendingLight*` / `IsPendingLight*` |
| FIFO work | Capture → async → Completed ring → `MarkRelit` → clear gate | `EnqueueTerrainColumnRelight` / `DrainRelightQueues` |

Gate without FIFO = ghost pending. FIFO without Completed = partial
Y-band / requeue (`finalize_gate=false`). PromoteRelight / SoftDefer
retarget currently **hop** the pin every frame instead of holding nh≤2
until `MarkRelit`.

## Phases and gates

Compare every inland −485/50 to `195810` **and** SLA. Worse wall = revert
the phase, not more knobs. Restore spawn `(-7752, 96, 808)` yaw 90 pitch 0.

### P0 — measure (no behavior)

Telem in perf jsonl (counters only, no extra scans):

- `relight_capture_col_horiz` / `relight_capture_finalize` /
  `relight_capture_band_cy_span` from `drain_one`
- `relight_witness_hold_n` (consecutive frames same miss cx/cz)
- `relight_apply_n` (DrainCompleted count this frame)

Classify bottleneck: hop | hitch-finalize | apply starve | worker inflight.
**Do not enlarge Capture until this is known.** Wall med/p90 must match
`195810` (telem-only).

### P1 — pin until MarkRelit (O(1), no extra Capture)

While witness nh≤2 and still `PendingLight`: FIFO front stays that
column; `RequestPromoteRelight` must not replace it; SoftDefer must not
retarget. Trim still skips `dist≤ RelightMissPinMaxHoriz()` (ring=4).

Cost: existing Priority sort; fewer Enqueue/retarget. **Do not** raise
`bg_cap`.

Gate vs `195810`: retarget growth << 192; sticky (−487, 48) does not sit
pending for 24 frames; `relight_drain_ms` med/p90 not worse; wall not
worse. If pending still does not fall, hop was not the bottleneck → P2
from P0 facts.

### P2 — surface-complete nh≤2 only

Unsplit `finalize_gate=true` **only** for the pinned witness **nh≤2**,
surface band only (`RelightSurfaceBandForColumn`). Rim nh=3–4 keeps
Y-band split (3 cy moving+holes) so drain max 597 does not return.

Do not copy enter (`EnterFovLitCaptureDrainMs=80`, inflight×12).
Apply: if workers busy and ring empty, check `RelightCompletedDiscarded`;
same-frame `DrainAsyncRelightResults` already 8–16 — do not MarkRelit-flood
Dirty.

Gate: miss nh≤2 pending clears in a few frames; drain p90 ≤ ~61; wall med
≤ 204; **no** new spike class >600 ms. If P0 showed hitch-finalize,
narrow span to cy=0 first (111/135 miss were cy=0), not more drain ms.

### P3 — FirstMesh after light (order, not quota)

After `MarkRelit` clears pending, DesiredStage is FirstMesh. cz=53 on
`195810`: `pl med=0`, miss 100%, fm ~220, admit 8.

Boost just-relit nh≤2 (and underfeet) to the head of FirstMeshQ. Do **not**
`keep_h=2` (opaque 882→472 on `183918`). Do **not** raise schedule 6→16.

Gate: `pending_light_n=0` + miss nh≤2 is short-lived; skip on that column
drops; `dirty_fm` may stay ~200; wall/`mesh_emerge` not worse. Rim nh=4–5
is not this phase’s DoD.

### P4 — keep-until-bind in vis+keep ring

Publish is already `ShouldPublishMeshToDraw(lit \|\| keep_prior_gpu)`.
`ae0c75b0` clears stale `GpuResident` without `HasGpuMesh` — honest, and
it emptied the pool (13.8→2 MB) when replace/unload freed slots with no
bind.

Do not `FreeChunk` / `RemoveChunk` a live GPU slot in visual/keep ring
until `BindCommitted` replacement. Hinterland still evicts (pool cap 256
MB). Hide-until-lit covers FullyDark in ring — dropping dark GPU «ради
дыр» is a second draw SoT.

Cost: ~14 MB like `163559` late pool; CPU ~0. Do not raise cap.

Gate: late cz≥55 `opaque_draw` med ≥200; pool not ~2 MB at same vis RD;
underfeet reason 7 not 49 frames straight. Only meaningful after P1–P3
produced at least one lit mesh underfoot.

## Unit / inland

Units: pin-hold, surface-finalize nh≤2 vs nh=4 split, FM sort boost,
defer FreeChunk in-ring. Inland World_164 −485/50 after each behavior
phase. Audit: `python bin/audit_cruise_sot.py <log>`.

## After P0–P4 (`215411`)

P0 class: **hop**, not apply-starve, not hitch>600. Hold never
accumulates (`hold_n` max 1) because `ShouldHoldPinnedRelightWitness`
requires pending **on the miss column**; miss is often cy=2 missing-mesh
while pending sits elsewhere. SoftDefer retarget 2→208. P4 keeps packed
~252 / pool ~14–16 MB through cz=54; cz=55 still dumps to ~2 MB / opaque
~111. Do not raise Capture/FM quotas — wall p90 already 355 vs 279.
Next: pin the miss key while `focus_missing_mesh` nh≤2 even without
pending on that key; keep GPU across the last +Z step. Details:
[`cruise_215411_diag.md`](cruise_215411_diag.md).
