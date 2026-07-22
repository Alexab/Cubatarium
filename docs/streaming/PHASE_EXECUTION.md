# Phase Execution Log

Автоматический учёт прогонов фаз streaming (autofly `--teleport-cruise`).

| Phase | Config | sticky | cold | spike_max | nr_end | fd_end | Notes |
|-------|--------|--------|------|-----------|--------|--------|-------|
| iter23_r2 | Debug | **0** | 6 | 788 | 51 | 454 | best verified Debug |
| manual_161304 | manual | **0** | **16** | **4662** | 27 | 330 | UX: seconds-scale mesh_emerge |
| stepCAB (aggressive) | RelWithDebInfo | 9 | 6 | 6589 | 90 | 616 | **anti-pattern**: sync_cap=0 + StarveRemesh → sticky |
| baseline_rel (3d6b033c) | RelWithDebInfo | 9 | 4 | 3101 | 89 | 676 | same sticky noise on Rel build |
| step_safe (this) | RelWithDebInfo | 9 | **2** | **1005** | 90 | 658 | vs Rel baseline: cold↓ spike↓ sticky same |

## Lessons (2026-07-22 evening)

1. **Aggressive C (sync_cap=0, ban underfeet, StarveRemeshForHoles)** → sticky↑ and spike↑. Do not repeat.
2. **RelWithDebInfo ≠ Debug** for sticky on current World_164 (Rel baseline sticky=9 while historical Debug r2 was 0). Prefer Debug for sticky gates when boot works; Rel for throughput.
3. **Debug enter-game-smoke / flight-sim** intermittently hangs after `[Log] initialized` (0-byte perf). RelWithDebInfo boots reliably.
4. Remaining tails vs manual 161304 still **A cold, B FPS, C spike**; safe path is incremental Rel/Debug A/B without touching SoftDefer/underfeet V2a.

## Safe patch contents

- `FocusIngressPolicy`: modest relight floor on hitch cold frames (8–12).
- `force_hole`: moving non-underfeet frame cap 22ms (underfeet stay 40).
- Moving no-hole: schedule≤6, drain≥24 when dirty>400.
- Gates: phase `C` / `CB` in `flight_sim_phase_gate.py`.
