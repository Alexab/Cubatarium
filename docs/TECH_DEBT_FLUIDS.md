# Tech debt: Fluids (flow-level refactor)

> Review at end of each phase (F0–F6, R1–R3). Close items when implemented or explicitly wont-fix.
> Total closed: 20 | Open: 4

## Open

| ID | Added in | Item | Why deferred | Target phase |
|----|----------|------|--------------|--------------|
| TD-FL-003 | R1 | Level-based basin / cliff face rules in `GreedyMesher` | R1 uses pre-F0 full-cube culling (fluid→solid hidden) | R4 |
| TD-FL-012 | R1 | `GreedyMeshEmitter` level-truncated fluid height | Simulation levels only until shore mesh tuned | R4 |
| TD-FL-021 | R3 | Manual QA checklist in `PHYSICS_ROLLOUT.md` (liquids) | Requires in-game verification | R3 |
| TD-FL-022 | R4 | Luanti-style sloped fluid mesh (`drawLiquidNode`) | Depends on stable transform sim | R4 |

## Closed

| ID | Closed in | Resolution |
|----|-----------|------------|
| TD-FL-001 | F2 | Replaced `LiquidSimulationSystem` with `UFluidSpreadSystem` (source/flowing levels) |
| TD-FL-002 | F5 | `MarkFluidRegionDirty` + immediate remesh near player |
| TD-FL-003 | F4 → R1 | Basin heuristic (reverted R1; see Open) |
| TD-FL-004 | F2 | Removed `BlockedReturnCells` / move-copy liquid model |
| TD-FL-005 | audit | `JsonChunkSerializer` writes/reads optional `fluid` byte per voxel |
| TD-FL-006 | F1 | Full `fluid_data[4096]` per chunk (no RLE) — acceptable size |
| TD-FL-007 | audit | `fluid_chunk_io_test` binary v1/v2 round-trip via `UBinaryChunkSerializer` |
| TD-FL-008 | audit | `WorldStateHasher` includes packed fluid in region hash |
| TD-FL-009 | audit | `UFluidSpreadSystem` records `ULiquidDebugTrace` on spread |
| TD-FL-010 | audit | DDA air-pocket heuristic in `RaycastFluidPlacementTarget` |
| TD-FL-011 | audit | `AddObjectByView` + `UpdateIntersection` use fluid placement raycast |
| TD-FL-012 | audit → R1 | Level height stub (reverted R1; see Open) |
| TD-FL-013 | audit | `fluid_blocks_per_tick_max` in `config.json.example` + `Core.cpp` |
| TD-FL-014 | audit | `fluid_placement_test` covers pit, old pit, capsule scenarios |
| TD-FL-015 | audit | `fluid_mesh_faces_test` counts GreedyMesher fluid faces |
| TD-FL-016 | audit | `ChunkMeshSnapshot` shell fluid layer + `GetFluid()` |
| TD-FL-017 | audit | `UBlockWorld::SetBlock(liquid)` auto-`Source()` via definitions hook |
| TD-FL-018 | R2 | `ScanChunkFluidFrontier` / `EnqueueFluidFrontierAt` (ReflowScan-style) |
| TD-FL-019 | R2 | Restored `LiquidRenewable` for water; transform creates sources from 2 neighbors |
| TD-FL-020 | audit | `ARCHITECTURE.md` flow-level opaque↔fluid section updated |
| TD-FL-023 | R2 | Luanti-style `TransformFluidCell` in `UFluidSpreadSystem` |
| TD-FL-024 | R3 | `fluid_queue_integration_test` via `UFluidUpdateSet` + budget 128 |

## Phase tracker

| Phase | Status | Notes |
|-------|--------|-------|
| F0–F6 | done | Initial flow-level refactor |
| R1 | done | Full-cube mesh + simple culling restored |
| R2 | done | Transform sim + ReflowScan + Source placement |
| R3 | done | Integration test, telemetry stub, docs |
| R4 | backlog | Level mesh + sloped surfaces |

## Related docs

- [FLUID_ARCHITECTURE.md](FLUID_ARCHITECTURE.md)
- [PHYSICS_ROLLOUT.md](PHYSICS_ROLLOUT.md)
