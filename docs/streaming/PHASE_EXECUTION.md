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
| manual_193627 | manual walk | **0** | 14 | **3042** | — | — | emerge_prep=relight_drain 0.9–3s (MeshEmerge cold drain 8–24) |

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

## Sync-budget + autofly parity (2026-07-22)

- Moving: no Immediate/SyncRebuild hole-fill; nearest → Dirty/async.
- Idle sticky: `SyncIdleFocusGreedyRemesh` → MarkDirty (not Immediate).
- Autofly: `--replay-manual` resumes save, `--hold-space`, pitch 0, default pitch −2.
- Cold hole: same-tick promote → `DrainRelightQueuesBudget`.
- SoftDefer / greedy unchanged. GPU meshing postponed.

## Safe patch contents

- `FocusIngressPolicy`: modest relight floor on hitch cold frames (8–12).
- `force_hole`: moving → Dirty only (no Immediate).
- Moving no-hole: schedule≤6, drain≥24 when dirty>400.
- Gates: phase `C` / `CB` in `flight_sim_phase_gate.py`.
