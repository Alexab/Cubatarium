# Tech debt: Fluids (flow-level refactor)

> Review at end of each phase (F0–F6). Close items when implemented or explicitly wont-fix.
> Total closed: 20

## Open

| ID | Added in | Item | Why deferred | Target phase |
|----|----------|------|--------------|--------------|
| — | — | *(none)* | — | — |

## Closed

| ID | Closed in | Resolution |
|----|-----------|------------|
| TD-FL-001 | F2 | Replaced `LiquidSimulationSystem` with `UFluidSpreadSystem` (source/flowing levels) |
| TD-FL-002 | F5 | `MarkFluidRegionDirty` + immediate remesh near player |
| TD-FL-003 | F4 | Basin heuristic + fluid level face rules in `GreedyMesher` |
| TD-FL-004 | F2 | Removed `BlockedReturnCells` / move-copy liquid model |
| TD-FL-005 | audit | `JsonChunkSerializer` writes/reads optional `fluid` byte per voxel |
| TD-FL-006 | F1 | Full `fluid_data[4096]` per chunk (no RLE) — acceptable size |
| TD-FL-007 | audit | `fluid_chunk_io_test` binary v1/v2 round-trip via `UBinaryChunkSerializer` |
| TD-FL-008 | audit | `WorldStateHasher` includes packed fluid in region hash |
| TD-FL-009 | audit | `UFluidSpreadSystem` records `ULiquidDebugTrace` on spread |
| TD-FL-010 | audit | DDA air-pocket heuristic in `RaycastFluidPlacementTarget` |
| TD-FL-011 | audit | `AddObjectByView` + `UpdateIntersection` use fluid placement raycast |
| TD-FL-012 | audit | `GreedyMeshEmitter` truncates fluid side faces by level height |
| TD-FL-013 | audit | `fluid_blocks_per_tick_max` in `config.json.example` + `Core.cpp` |
| TD-FL-014 | audit | `fluid_placement_test` covers pit, old pit, capsule scenarios |
| TD-FL-015 | audit | `fluid_mesh_faces_test` counts GreedyMesher fluid faces |
| TD-FL-016 | audit | `ChunkMeshSnapshot` shell fluid layer + `GetFluid()` |
| TD-FL-017 | audit | `UBlockWorld::SetBlock(liquid)` auto-`Source()` via definitions hook |
| TD-FL-018 | audit | `ChunkPhysicsSeed` enqueues source/frontier liquids only |
| TD-FL-019 | audit | Removed `LiquidRenewable` / `IsLiquidRenewable`; reactions use `FluidMaxLevel` |
| TD-FL-020 | audit | `ARCHITECTURE.md` flow-level opaque↔fluid section updated |

## Phase tracker

| Phase | Status | Last commit | Debts closed |
|-------|--------|-------------|--------------|
| F0 | done | audit | 0 |
| F1 | done | audit | 3 (TD-FL-006,007,017,018) |
| F2 | done | audit | 4 (TD-FL-001,004,008,009) |
| F3 | done | audit | 3 (TD-FL-010,011,014) |
| F4 | done | audit | 4 (TD-FL-003,012,015,016,020) |
| F5 | done | audit | 2 (TD-FL-002,013) |
| F6 | done | audit | 2 (TD-FL-005,019) |

## Related docs

- [FLUID_ARCHITECTURE.md](FLUID_ARCHITECTURE.md)
- [PHYSICS_ROLLOUT.md](PHYSICS_ROLLOUT.md)
