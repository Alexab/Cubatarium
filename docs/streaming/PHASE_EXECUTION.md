# Phase Execution Log

Автоматический учёт прогонов фаз streaming (autofly `--teleport-cruise`).

| Phase | Config | sticky | cold | spike_max | nr_end | fd_end | Notes |
|-------|--------|--------|------|-----------|--------|--------|-------|
| iter23_r2 | Debug | **0** | 6 | 788 | 51 | 454 | best verified Debug |
| manual_161304 | manual | **0** | **16** | **4662** | 27 | 330 | UX: seconds-scale mesh_emerge |
| stepCAB (aggressive) | RelWithDebInfo | 9 | 6 | 6589 | 90 | 616 | **anti-pattern**: sync_cap=0 + StarveRemesh → sticky |
| baseline_rel (3d6b033c) | RelWithDebInfo | 9 | 4 | 3101 | 89 | 676 | same sticky noise on Rel build |
| step_safe | RelWithDebInfo | 9 | **2** | **1005** | 90 | 658 | mild hitch floor |
| sync_budget_r1 | RelWithDebInfo | 9 | 4 | **4401** | 90 | 698 | ban Immediate + full ring MarkDirty flood — regress |
| sync_budget_r2 | RelWithDebInfo | 9 | 8 | **978** holes | 25 | 365 | no moving Immediate; nearest Dirty only |
| replay_manual_r1 | RelWithDebInfo | 9 | **0** | holes **160** / wall 1373 | 12 | 198 | resume −473; SyncIdle→Dirty; hold-space; stream hitch |
| manual_194645 | manual walk existing | **0** | 10 | **4288** | 31 | 347 | prep=relight 3–4s (MeshEmerge drain+Capture); quiet wall~28 dirty~524 async=42 |
| mem_214430 | manual standing | — | — | — | — | — | remesh thrash `async≈42`; Private→20+ GB; telemetry rss/private |
| mem_220018 | idle + place light | — | — | **15–52s** | — | — | idle Capture storm `hole_cap 48–56`; schedule≤4 too aggressive |
| mem_221846 | place lit block | — | — | hang | — | — | unbounded light BFS outside HasChunk → fixed `152cb5df` |

## Memory crisis (2026-07-22, Era 12)

1. **Fluid `GetOrCreateChunk`** into missing → resident explosion (`02b9868d`: HasChunk).
2. **ForgetInflight without DrainAll** → orphan Completed mesh RAM (`0cb92063`).
3. **Standing remesh latch / async≈42** → Dirty thrash + Private 20+ GB (`214430`, latch `b1f8924c`).
4. **Idle Capture without time budget** (`hole_cap 48–56`) → 15–52 s spikes (`220018`).
5. **Light BFS into missing chunks** (Write no-op, GetLight=0) → hang + RAM on place light (`221846` / `152cb5df`).
6. Next: byte-budget + fill% — [`MEMORY_BUDGET.md`](MEMORY_BUDGET.md).

## Memory budget implementation (2026-07-22)

Landed: RuntimeTuning Soft/Budget knobs, Completed drop-oldest rings, Dirty/
Pending/FIFO soft-caps, GPU Reserve/Max, `MemoryBudgetController`, UChunk free-list.
Gates: see Validation notes in `MEMORY_BUDGET.md` (manual place-light + autofly
private p95 / fill% / wall).

## GPU pipeline init-bind (2026-07-25)

Phase 0–5 scaffolding: `URenderBackendFactory::BindOnce`, `IUChunkMesher` /
`IUMeshGpuStore` / `IUChunkCull`, `EditMeshRemeshPolicy`, MDI store + vertex-pool
free-list, GPU mesher/light/fluid wrappers (parity). See [`GPU_PIPELINE.md`](GPU_PIPELINE.md).
Unit gates: `edit_mesh_remesh_policy_test`, `render_backend_factory_test`,
`mesh_gpu_store_mdi_test`.

`gpu_p1` autofly (`551a7766`, PreferGpuStorePatch wired): backends
`cpu_greedy` / `mdi_vertex_pool` / `gpu_frustum` confirmed in jsonl. F2 NO-GO on
pre-existing `sticky=9`, `cold_relight_holes_sec=16`, `focus_dirty_end=403`
(stream class) — not a draw-path regression from init-bind.

`gpu_mdi` smoke (`baseVertex` + texture-grouped `glMultiDrawElementsIndirect`):
backends same; `gpu_draw_cmds` med≈360 (per-indirect-cmd count), pool fill med≈0.06.
F2 still NO-GO (`sticky=9`, `cold_relight_holes_sec=18`, stream class) — draw path OK.

`gpu_mdi_sort`: opaque/cutout sorted by `blockId` before pool refresh;
`gpu_draw_cmds` now counts API submits — med **8** (max 14). F2 still stream NO-GO.

### F2 sticky drain (2026-07-25)

`SyncIdleFocusGreedyRemesh` moved outside `wall≤28` visual-drain gate (stop-tail
wall often 40–55 ms). Short autofly: **sticky 9→0**. Remaining F2 fails:
`cold_relight_holes_sec` (6–16) and `post_stop_focus_dirty_end` (~400 vs ≤280).
`idle_focus_dirty_debt` (fd>280, nr≈0) starves outside + drain/schedule bias —
fd falls on short stop (`fd_delta≈-100`) but golden stop still ends ~400.

### Manual follow-up (2026-07-23)

| Run | Notes |
|-----|-------|
| `081832` | private≈0.5 GB; Soft stuck via stream Yellow → decouple Soft from stream |
| `085228` | Soft=0 OK; keep_margin→3; Dirty~350–470 no drops; Capture hitch 2.9–4.5 s |
| `091724` | Soft=0; keep→3; Dirty~400–590 async≤29 thrash miss; Capture~3.2 s holes=1 |
| `102936` | Soft=0; Dirty SoftCap works (`dirty_dropped`); wall med 38 ms; Capture max~1.6 s |
| `105049` | Y-band: period drain max~1.5 ms; spike wall max~1 s (was 1.6 s) |
| `152216` | Soft=0; private~480; sticky=0; rare Capture hitch~2.2 s holes=1 |

Tails after `102936`: SoftDefer-safe **Y-band Capture** (`RelightCaptureBandCy`,
`finalize_pending_gate`) — PendingLight until final band.

### Fog edge masking (2026-07-23)

Landed: stronger `distance_fog_end_margin_blocks` (28), earlier start ratio (0.48),
air fog while submerged (non-fluid), fog-only `EffectiveFogRenderDistance` pull-in
(`fog_pull_in_enabled` / `fog_rd_min`). Manual `161702`: unfinished max≈5 never
hit old threshold `UnfinishedVisual>8`, and saved config still had start_ratio 0.85 /
margin 12 → trees popped clear then fog lagged. Follow-up: pull-in on any unfinished,
dynamic margin/start under debt, refresh local config defaults.

Manual: walk/dive at visual edge; Yellow should shorten fog End without shrinking
mesh RD; incomplete surface should stay fogged, not clear-then-fog.

### Sync break-glass → async FIFO (2026-07-23)

Manual `164613`: spikes ≥1 s had `stream_ms` small and
`mesh_emerge_prep≈relight_drain` — root cause was
`DrainIdleFocusPendingLightSync` → `RelightTerrainColumn` (full-column sync BFS),
not Y-band Capture. With `AsyncRelight`, Sync now priority-enqueues FIFO only;
paced `DrainRelightQueues` keeps Capture. Expect: no multi-second `prep` spikes;
holes may linger slightly longer under SoftDefer+fog.

### Fog water unfinished A+B (2026-07-23)

Plan: [`FOG_WATER_UNFINISHED.md`](FOG_WATER_UNFINISHED.md). Near fluid/sea +
holes/unfinished → stronger fog pull-in (`fog_water_start_ratio_cap` 0.28) and
wider sky horizon (`FogHorizonElevation` 0.22). No proxy quads.

### Fluid map budget + scroll (2026-07-23)

Plan: [`FLUID_MAP_BUDGET.md`](FLUID_MAP_BUDGET.md). Manual `201036`: spikes
`fluid_map_cpu` 400–800 ms. Wall-aware chunk budget (≤8 after wall>40 ms) and
`surface_window_move_threshold` 16→32.

### Perf research autofly (2026-07-23)

Cheap metrics + formula fix: `world_extra_ms = max(0, LastWorldTick − PhysicsStep)`
(was double-subtracting stream/mesh already inside phys). New scopes:
`tick_env_ms` / `block_input_ms` / `views_ms`; break counters; soft spike buckets
in `flight_sim_analyze`.

| Run | Config | sticky | cold | spike_max | dominant (≥500) | Notes |
|-----|--------|--------|------|-----------|-----------------|-------|
| `research_r1` | Rel `--replay-manual` | 9 | 2 | 1945 / holes 782 | **stream** | `spike_max_world_extra≈0.02`; tick_env/block_input ~0; CB NO-GO sticky+holes+wall_no_holes 36 |
| `research_r3` | Rel `--scenario break-stand` | 0 | 0 | 494 | — | `break_complete=22`, **`break_inflight_race=20`**, `break_dark_face=0` |

Conclusion R1: former ~800 ms `world_extra` spikes were **telemetry residue**, not
TickEnvironment/BlockInput. Heavy hitch class = `stream_ms`. Soft WE gate OK.

Conclusion R3: inflight race strongly correlates with break edits (20/22). Immediate
path now bumps mesh revision + clears `ActiveMeshSourceRevision` (flicker fix).
Dark-face counter stayed 0 after local Relight+Immediate (blackness may need
full ClearColumn path or later frames). Fluid cold pending throttle landed.

### Edit black flash + FastRelight hitch (2026-07-23)

Manual `213640`: OK→black→OK ~1s after dig; place lag ~100–300ms =
`edit_to_first_mesh`. Fix: SoftDefer remesh of existing mesh while
`PendingLight` even underfeet; `ApplyEditLighting` notes Pending until MarkRelit;
FastRelight Clear only edit column ±1 cy (not 3×3); break Immediate center-only.

Follow-up: dig OK alone, but dig→place nearby black/x-ray — second FastRelight
Clear on pending (±1) column re-Immediate’d the dig mesh dark. Skip FastRelight
while any edit/neighbor column still PendingLight.

### Edit light systemic (2026-07-23 evening)

Manual `222250`: after dig `black_sticky` ~48s / `focus_dark_mesh` ~54s; hitch
`edit_to_first_mesh`~380ms; night torch dig showed stale/wrong brightness
(`clear_first=false` + monotonic block light). SoftDefer-as-edit-lock froze the
wrong Immediate until MarkRelit.

Fix: `RelightBlocksAroundEdit` (sky/block remove+flood, radius ≤15, no chunk
Clear) before Immediate; `ApplyEditLighting` only enqueues async player-relight
for seams — no `NotePendingLight` / SoftDefer lock on dig/place. Streaming
SoftDefer for cold Pending unchanged. Center Immediate only.

Follow-up manual `224642`: place lamp stayed dark until another edit (opaque
emitter wiped after flood seed); night dig near light black; FastRelight 31³
emitter scan hitch. Fix: classic remove from edit cell only; keep origin
emission; seed face neighbors; drop radius emitter scan. Perf jsonl:
`place_complete_n`, `place_emission_n`, `edit_light_emission`, `fast_relight_ms`.

Manual `230913`: fast_relight cheap (~0.2–2ms) but dig `edit_to_first_mesh`~110ms
with `mesh_async=42` — neighbor faces stayed dark / lamp glow dripped in via
async Dirty. Fix: dig/place Immediate face neighbors; emission edits also sync
a capped light-ring remesh (≤9 chunks) + remesh burst.

Manual `092611`: dig OK, place in hole / on rim → temporary blackness on dig
faces that self-clears. Cause: `EnqueuePlayerRelight` Cleared neighborhood and
MarkRelit remeshed dark until later bands; also dig inflight Apply racing place.
Fix: skip async Clear player-relight after incremental edit; invalidate edit
neighborhood inflight before Relight.

## Lessons (2026-07-22 evening)

1. **Aggressive C (sync_cap=0, ban underfeet, StarveRemeshForHoles)** → sticky↑ and spike↑. Do not repeat.
2. **RelWithDebInfo ≠ Debug** for sticky on current World_164 (Rel baseline sticky=9 while historical Debug r2 was 0). Prefer Debug for sticky gates when boot works; Rel for throughput.
3. **Debug enter-game-smoke / flight-sim** intermittently hangs after `[Log] initialized` (0-byte perf). RelWithDebInfo boots reliably.
4. Remaining tails vs manual 161304 still **A cold, B FPS, C spike**; safe path is incremental Rel/Debug A/B without touching SoftDefer/underfeet V2a.
5. **Moving `RebuildChunkImmediate` ban** kills seconds-scale `mesh_emerge` (r1→r2: emerge max 4.3s→0.2s). One greedy column cannot be ms-budgeted mid-call.
6. After Immediate ban, autofly top wall often **`stream_ms`**, not mesh — separate hitch class.
7. Telemetry: `mesh_immediate_ms` / `mesh_immediate_count` / `mesh_dirty_tick_ms` / `mesh_emerge_prep_ms` in perf jsonl.
8. **`SyncIdleFocusGreedyRemesh` Immediate** was a hidden seconds hitch (manual 190126 emerge~3.7s, imm wiped by Reset). Now MarkDirty→async.
9. **`--replay-manual` must resume save** (no teleport to −47). Teleport-cruise ≠ manual corridor (−473/−484).
10. **manual 194645:** MeshEmerge cold `DrainRelightQueuesBudget` while moving → `relight_drain` 3–4s (`Capture`/sync column). Quiet: `idle_remesh_debt` + snapshot 48ms + async≈42 → wall~28 at rest.
11. Smooth (no SoftDefer/greedy change): MeshEmerge promote-only while moving; Streaming move-cap ≤2 async, no sync drain; lower FocusIngress floors; raise idle_remesh thresholds; snapshot 48 only for holes/pending.
## Sync-budget + autofly parity (2026-07-22)

- Moving: no Immediate/SyncRebuild hole-fill; nearest → Dirty/async.
- Idle sticky: `SyncIdleFocusGreedyRemesh` → MarkDirty (not Immediate).
- Autofly: `--replay-manual` resumes save, `--hold-space`, pitch 0, default pitch −2.
- Cold hole while moving: **Promote only** (no MeshEmerge Drain — Capture/sync = seconds).
- Streaming walk: async enqueue ≤2/frame + 4ms loop budget; never sync Relight while moving.
- SoftDefer / greedy unchanged. GPU meshing postponed.

## Safe patch contents

- `FocusIngressPolicy`: paced walk floor (1–4), not 36–48.
- `force_hole`: moving → Dirty only (no Immediate).
- `idle_remesh_debt`: nr>32 / fd>80; snapshot 48 only for holes/pending light.- Moving no-hole: schedule≤6, drain≥24 when dirty>400.
- Gates: phase `C` / `CB` in `flight_sim_phase_gate.py`.
