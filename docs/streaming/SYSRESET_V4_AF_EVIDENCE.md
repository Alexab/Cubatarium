# Sysreset v4 AF evidence (fog ON)

Date: 2026-09-21  
Commits: `eca9289c` FaceDebt+PreferKick; `83a7b8dd` hitch C  
SoT manual: `perf_20260921-101105_14028.jsonl`  
Gates: [SYSRESET_OPERATOR_AF_GATES.md](SYSRESET_OPERATOR_AF_GATES.md)

## AF suite

| Run | perf | scorecard |
|---|---|---|
| cold | `bin/logs/perf_20260921-105534_4864.jsonl` | `sysreset_v4_cold_score.json` |
| warm | `bin/logs/perf_20260921-105727_25268.jsonl` | `sysreset_v4_warm_score.json` |
| dive | `bin/logs/perf_20260921-105919_5900.jsonl` | `sysreset_v4_dive_score.json` |

## Gate matrix vs 101105

| Gate | 101105 | cold | warm | dive | Verdict |
|---|---|---|---|---|---|
| west | COVERED | COVERED | COVERED | COVERED | PASS |
| VB fly med ≤35 | 44.5 | 46 | 38 | **32** | dive PASS; cold/warm OPEN |
| mid_fd_stalled (dual_lane) ≤5 | 11–19 | 10 | 6 | 11 | OPEN (↓ vs SoT; mid_stalled_gate PASS) |
| unfinished max ≤4 | 63 | 78 | 80 | 79 | OPEN |
| PreferKick stand/fly >0 | ≡0 | ≡0 | ≡0 | ≡0 | OPEN (path coded; AF still 0) |
| dirty_fm med ≪90 / Δ↓≥30% | ~87–93 | 140 | 118 | 118 | OPEN (admit drops↑, FM still high) |
| prior_lit_hold fly med ≤400 | ~8 | **8** | **10** | **7** | PASS KEEP |
| flip / dual ≡0 | 0\|0 | 0\|0 | 0\|0 | 0\|0 | PASS |
| mesh_gpu_kick max ≤20 | ≪1 | **4.8** | **3.9** | **4.7** | PASS |
| mesh_emerge fly max ≤40 | ~5 | **8.4** | **8.7** | **8.7** | PASS |
| render_total fly max ≤80 | 79–200 | **61.6** | 80.3 | 90.0 | cold PASS; warm/dive OPEN |
| spikes max ≤2 | 2 | **2** | 3 | 4 | cold PASS; warm/dive OPEN |
| opaque cull (scene) | spikes class C | **~0.03 / skip=1** | skip=1 | skip=1 | hitch C WIN |
| transparent reorder mid ≤0.5 | 0–1 | **0** | **0** | 0.5 | PASS |
| eye-proxy | — | PASS | PASS | PASS | PASS |
| merge_green | — | false | false | false | OPEN (operator UNTESTED + unfinished/PreferKick) |

## Wins

- Hitch C: `opaque_cull_skipped≡1`, `scene_opaque_cull_ms` ≪ SoT class-C spikes; cold `render_total` fly max ≤80.
- West COVERED + eye-proxy PASS on cold/warm/dive fog-ON.
- Kick/emerge/prior_lit KEEP (v3 ledger intact).
- FaceDebt/BecameKnown + PreferKick/Dirty-admit coded and unit-tested.

## Still OPEN (no SoftDefer spoof)

- PreferKick N ≡0 on AF despite Kick/Finish `NotePublishProgress` — pending-GPU window rarely pairs with FD drawable after Dirty admit thrash (`dirty_dropped` ~70k).
- unfinished / focus_not_render_ready med ~55–65 (sky-through census residual).
- VB fly ≤35 only on dive; cold/warm still above.
- warm/dive spikes max >2; dive render_total fly max 90.
- merge_green blocked until operator eye + PreferKick/unfinished gates close.

## Operator note

Sky-through closed only after manual eye confirms seam remesh; AF cannot alone mark operator sky CLOSED.
