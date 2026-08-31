# I18: Rim chain closure + witness comfort

**База:** commit `956aac19` (I17 P0–P3)  
**Gate-of-record:** `perf_20260831-214536_23384.jsonl`  
**Blink SoT:** `perf_20260831-144853_32564.jsonl`

## Реализовано (bisect order A → F → B → C → D → E)

### I18-A — FM→GPU chain
- **A1:** `ProcessPendingGpuMeshes` — watch+rim coords первыми в finish partition; telem `gpu_finish_watch_rim_n`
- **A2:** `RimChainStallKickFrames` — kick 4f (2f при schedule_starved) в `ChunkEmergeCoordinator`
- **A3:** consumer-starved bypass — carve-out nh 2–3 (`MeshWorkAdmission`) + schedule floor (`ChunkEmergeCoordinator`)
- **A4:** telem `fm_dirty_gpu_watch_*`, harvest в `FramePerfMonitor`

### I18-F — IngressDebt governor
- **F1:** `EvaluateIngressDebt` в `StreamIngressPolicy.h`, wiring в emerge tick
- **F2:** `capture_hard_cap=1` при debt≥ShedFar (`MemoryBudgetController`)
- **F3:** `DynamicKickCutBiasForFmWatch` на `kick_cut`
- **F4:** fog pull-in при chain debt (`hole_debt_now` расширен)
- **F5:** `ShouldRateLimitWitnessRetargetUnderDebt` на Site B
- **F6:** `FrameStreamingBudget` — cap Capture при debt+nh>3

### I18-B — Witness throughput
- **B1:** Site A pin SLA + ingress GPU pending parity
- **B2:** `ShouldAllowBetterHorizWitnessRetarget` + sched gate
- **B3:** ring resync guard на witness hop alone
- **B4:** `UnfinishedSampleCooldownFramesCruise` + rim diet nh 2–4

### I18-C — Emerge guard
- **C1:** `prep_column_flow_drain_ms` subtimer
- **C2:** emerge budget ×0.85 при prior `prep_refresh_gap_ms < 30`

### I18-D — Visual comfort
- **D1:** `WitnessSwapGrace` — hold prior column drawable 2f на swap
- **D2:** overlap с A1 finish carve-out
- **D3:** revision defer на witness-hop window

### I18-E — Forensics
- `blink_window_audit.py`: классы `short_hole`, `chain_stall`
- Этот memo

## Gates (ручной пролёт vs `214536`)

| Gate | Target |
|------|--------|
| A | `fm_dirty_to_gpu_finish_med ≥ 1`, `chain_stall_sec ≤ 2`, `stream_ms ≤ 92` |
| B | `EH_blink ≤ 0.05`, `witness_hop ≤ 4` |
| D | `EH_blink ≤ 0.02`, short_hole_runs ≤ 2 |

## I19 — отложено

Geometric LOD L1/L2/L3 — только после I18 gates; pseudo-LOD в F6.
