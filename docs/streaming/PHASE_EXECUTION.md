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
