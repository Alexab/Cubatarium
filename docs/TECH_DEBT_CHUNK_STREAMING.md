# Tech debt: Chunk streaming & performance

> Review at end of each phase (A–E). Close items when implemented or explicitly wont-fix.

## Open

| ID | Added in | Item | Why deferred | Target |
|----|----------|------|--------------|--------|
| TD-CS-002 | 2026-06 | MarkDirtyColumn called per-stage, per-Y loop | Deferred dirty batching not yet applied | Phase A2 |
| TD-CS-003 | 2026-06 | No frame timing breakdown for gen/mesh/io | Profiler fields not wired yet | Phase A3 |
| TD-CS-004 | 2026-06 | Greedy mesh rebuild blocks render thread | Async meshing not integrated | Phase B |
| TD-CS-005 | 2026-06 | Worldgen blocks movement thread during streaming | Async populate not wired | Phase C |
| TD-CS-006 | 2026-06 | Sync JSON chunk I/O on load/unload | Background I/O not implemented | Phase D |
| TD-CS-007 | 2026-06 | FBM3D per-voxel in caves without gating | Cave gate / micro-opt pending | Phase A4/E3 |
| TD-CS-008 | 2026-06 | WorldGenContext holds MeshCache pointer | Decouple on async commit path | Phase C6 |
| TD-CS-009 | 2026-06 | No generation token / stale async results | Token registry not added | Phase C2 |

## Closed

| ID | Closed in | Resolution |
|----|-----------|------------|
| TD-CS-001 | 2026-06 | `MarkDirty` bumps revision only when chunk newly enters dirty set |

## Phase tracker

| Phase | Status | Last commit |
|-------|--------|-------------|
| Setup | done | docs: add chunk streaming tech debt tracker |
| A | in progress | — |
| B | pending | — |
| C | pending | — |
| D | pending | — |
| E | pending | — |
