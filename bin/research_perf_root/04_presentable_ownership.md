# Presentable ownership (Phase 5.5)

## Contract

- `soft_force` @ `EnterForceInGameMs` (150s) is a **last-resort wall**, not a success metric.
- `soft_force` + `visibility_debt>0` ⇒ **FAIL** in Phase55 scorecard until PresentableCatchUp drains debt (or settle is live/soft with debt=0).
- Autofly is a **proxy** only after fidelity checklist green; gate of record for UX ship = manual eye vs SoT `perf_20260906-170813_25272`.

## Owners

| Concern | Owner | Must not |
|---|---|---|
| Enter FOV presentable debt | Enter session / GpuWarmup + `CountEnterVisibilityDebt` | Exit via Quiesce/`coop_prepared` with debt>0 |
| SoftDefer empty placeholder | SoftDefer scan + FirstMesh/Dirty → PendingGpu | `SoftDeferEmptyOwned` forever without GPU/drawable progress |
| Rim FirstMesh heal | Emerge admission + presentable carve | Heal only via AbortDrip under permanent abort |
| GPU apply | `ProcessPendingGpuMeshes` / PendingGpuApplies | PreferKick with empty queue as progress |
| Hinterland relight | WorldStreaming DrainRelightQueues | Starve near mesh when `visibility_debt>0` / FocusMissing |

## Telemetry axes (do not conflate)

| Field | Meaning |
|---|---|
| Enter HB `fifo` / `inflight` | **Relight** FIFO / async inflight |
| `mesh_async` | Bool: mesh async blocks ring |
| `gpu_kick` / `gpu_finish` | Mesh PendingGpuApplies only |
| `visibility_debt` / HB `debt` | Enter presentable / unready columns |

## Phase55 harness

- No-teleport only: `--replay-manual[-fly-heavy]`, `fz-cold-enter`. **Not** `--land-stand`.
- Always `--process-timeout 600` (soft_force@150s + fly + stop).
- Analyze: `tools/AnalyzePhase55Scorecard.py` with `--baseline-manual` 170813.

## Phase 5.6 ownership (ring frontier)

| Concern | Owner | Must not |
|---|---|---|
| PresentableCatchUp latch | soft_force+debt arm → remesh mesh-but-!VisualReady in R=4 → clear when debt/ring/underfeet | Latch forever while mesh exists but !VisualReady |
| Keep-ring FirstMesh | `FmDirtyEnqueueReserve` / presentable carve (r≤4) | Heal only via AbortDrip under permanent abort |
| Miss witness progress | Dirty/FM when FocusMissing && !PendingGpu | PreferKick empty queue as progress |
| Stand ahead honesty | Dense facing sample when !moving && miss/stuck | Stale `last_ahead` via cd=12 |

Gate of record SoT: `perf_20260906-192816_24828`. Analyze: `tools/AnalyzePhase56Scorecard.py` (baseline 192816). Locus: land save + yaw 90; no teleport.
