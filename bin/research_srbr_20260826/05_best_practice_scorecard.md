# 05 — Best practice scorecard

Дата: 2026-08-26 · HEAD `1f3f7675`  
Опоры: `ARCHITECTURE_OPTIONS.md`, `relight_then_mesh_industrial.md`, voxel scheduler / backpressure / open-world streaming patterns, evidence `04_hotspot_matrix.md`.

| # | Критерий | Score | Evidence | Gap |
| --- | ---: | --- | --- | --- |
| 1 | Single scheduler authority | **PARTIAL** | ColumnFlow tickets + MeshWorkAdmission + SoftDefer callbacks | Три authority на admit/schedule; ghost Dirty вне ColumnFlow |
| 2 | Explicit producer-consumer backpressure | **PARTIAL** | Capture floors, earned apply caps, SoftDefer KEEP | Producer floors живы (floor≈2) при consumer busy на **wrong** work (ghost Dirty) |
| 3 | No duplicate O(N) scans per epoch | **FAIL** | VB Count in Refresh; Unfinished Near; SoftDefer empty disk; possible ColumnFlow recount | Нет single-source scan cache per epoch |
| 4 | Trim/drop amortized ≤ O(N log N) | **PARTIAL** | `TrimFarRelightFifoFarthest` while+full scan per drop | Алгоритм O(N·drops); на 112418 stand не binding (PL drop≈0) |
| 5 | Async stale rejection safe | **PASS** | P17 miss_undrawn hold; discard/spike≈0.002 | Was FAIL on 100413; cured |
| 6 | Self-healing missing mesh | **FAIL** | miss_frac=1.0 на 5094 stand spikes; unf 66→71 | Scheduler не закрывает nh=0 underfeet |
| 7 | Bounded per-frame finalization | **FAIL** | stream≈41 vs budget~24; wall≈135; phase_budget_over frequent on cruise | Stand never returns to budget |
| 8 | Publish keep-until-replace without holes | **PASS** | MeshApplyPolicy; discard cured; keep-until-replace SoT | Eye holes remain from missing/unlit, not FreeChunk flash |
| 9 | Priority by distance+view, not thrash retarget | **PASS** (SoftDefer) / **FAIL** (Dirty) | SiteB≈0.07 SoftDefer KEEP | Dirty ghost thrash + frozen pending_cols rim |
| 10 | Telemetry reason-coded drops | **PARTIAL** | skip_orphan/softdefer/locked/… counters exist | Нет per-column reason timeline; orphan = !HasChunk only |
| 11 | Separate visibility list vs render list | **PARTIAL** | opaque_refs vs unfinished; VB ticketed vs no_ticket | Ticketed VB≈107 при no_ticket≈6 — visibility ≠ healed |
| 12 | No control-plane metric poisoning | **PASS** | P14 F5 deadband reverted; never re-land | Guard in research constraints |

## Summary
- **PASS (4):** #5 stale reject, #8 publish, #9 SoftDefer retarget, #12 no poison  
- **PARTIAL (5):** #1 #2 #4 #10 #11  
- **FAIL (3):** #3 duplicate scans, #6 self-heal miss, #7 frame budget  

**Top-5 gaps (actionable):**
1. Self-heal missing mesh (#6) — H1/H3  
2. Frame budget on stand (#7) — H4  
3. Scheduler authority / ghost Dirty (#1) — H1/H2  
4. Duplicate scans (#3) — H7 tech debt  
5. Ticketed VB vs render-ready (#11) — H3  

## Industry alignment notes
- Scheduler-driven mesh (re-queue missing if data+neighbors ready) — **violated** by sticky miss + ghost Dirty.
- Backpressure should slow **wrong** producers — Capture floors OK, Dirty re-admit of !HasChunk is not.
- Open-world: async finalize budget — stream 41ms on stand exceeds contract.
- RelightThenMesh industrial: FirstMesh after light for witness nh≤2 — sticky nh=0 should be highest priority, yet ok≈6 loses to orphan RemoveAt≈54.
