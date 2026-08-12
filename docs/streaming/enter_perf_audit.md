# Enter-load performance audit (Era44)

Manual reference run: World_174, log `20260812-153650`, enter_lit `20260812-153720.jsonl`.

## Timeline

| Phase | Wall | Progress bar | Notes |
|-------|------|--------------|-------|
| Cooperative load | ~30 s | 0–93%, relight/mesh messages | 574 chunks, bulk relight ~17 s |
| `phase=done` | instant | jump to ~93% | coop mesh_warmup ~4 s |
| `EnterGameGpuWarmup` | **~120 s** | stuck ~99% «Uploading terrain…» (pre-Era44) | `TickEnterGateMeshDrain` 0.9–1.5 s/frame |
| mesh abort (Era43f) | 15:39:22 | — | forced InGame with residual blockers |
| `first_paint` | 15:39:22 | screen clears | `mesh_dirty=79`, `mesh_in_flight=18` |

Era44 fixes: honest 93→100% debt bar, mesh abort → `abort_drain` (gate stays until `IsSpawnMeshRingReady()`).

## Dominant step (gpu_warmup)

From manual jsonl / glog (pre-profile instrumentation):

| Step | Avg ms/frame | Max ms/frame | % of frame wall (est.) |
|------|--------------|--------------|------------------------|
| `TickEnterGateMeshDrain` (6× emerge) | 900–1500 | ~1500 | **75–85%** |
| `DrainEnterGameMeshWarmup` | 50–200 | ~300 | 5–15% |
| `TickEnterFovLitPass` | 10–80 | ~120 | 5–10% |

**Dominant bucket:** `gate_drain` (>40% wall). Profile summary now emitted as `[EnterWarmup] profile` when ring becomes ready.

## Redundancy hypotheses (R1–R7)

| ID | Hypothesis | Verdict | Evidence |
|----|------------|---------|----------|
| R1 | Coop relight + gate FIFO duplicate columns | **Likely** | `relight_columns` ~17 s coop, then `fifo_n=79` at gpu_warmup frame 0 |
| R2 | 6× `TickMeshEmerge` per frame | **Confirmed** | `gate_drain_ms` ~1 s, fifo −1..−2/frame; knob `enter_gate_mesh_drain_iterations` added |
| R3 | `DrainEnterGameMeshWarmup` + gate overlap | **Partial** | Both active; gpu_pending oscillates 8–19 |
| R4 | Relight→MarkRelit→remesh loop | **Suspected** | fifo↓ but gpu_pending flat; watch `stage_skip_remesh_pending_light` |
| R5 | Partial Y-band Capture requeue | **Inconclusive** | needs `RelightFalseClearN` from live profile |
| R6 | Snapshot ghost vs FIFO | **Observed** | `snapshot_debt=0` while `fifo_n>0` for extended period |
| R7 | GPU apply starvation under gate | **Suspected** | `gpu_pending` flat 8–19 while `mesh_emerge_ms` high |

## Root cause statement

Enter-load wall time is dominated by **`TickEnterGateMeshDrain` (6× full emerge pipeline per frame)** while relight FIFO and GPU pending drain slowly; Era43f mesh abort masked the stall by forcing InGame with holes.

## Era44 quick wins (implemented)

1. Honest combined progress (fifo/gpu/ring/lit) on gpu_warmup bar.
2. Mesh abort → `abort_drain` until `IsSpawnMeshRingReady()` (no `EndEnterLitGate` on abort).
3. `BeginEnterLitGate` at end of coop mesh_warmup (Load) + drain under coop `PrepareView`.
4. Per-step forensics: `RecordFrameSteps`, heartbeat churn counters, `[EnterWarmup] profile`.
5. Tuning: `enter_gate_mesh_drain_iterations`, `enter_force_ingame_ms` (last resort ≥300 s).

## Before/after verification

Run:

```text
cd bin
.\Cubatarium.exe --console --autoload-last-world --visible --timeout-sec 300
```

Check: bar moves 93→100% with fifo/gpu/ring status; no InGame with `mesh_dirty>10` unless `force_ingame`; `enter_lit_*.jsonl` has `heartbeat` + `profile` lines.
