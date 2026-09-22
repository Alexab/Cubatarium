# A27 S5 — P5/P6 hot-path wiring

Date: 2026-09-22

## P5

- `ResumableWorkCursor` advanced each dirty rebuild (`ChunkMeshCache`).
- `ApplyUnifiedAdmissionPools` clamps admit when GPU/queued pools saturated (`MeshWorkAdmission`).
- `CapDirtyAdmitUnderThrash` KEEP (dirty_dropped ≤800 soft gate).

## P6

- Fluid full-scan: hitch estimate + `ShouldRejectFluidMapHitch` + worker enqueue stub.
- `FluidMaterialIdentityChanged` on pack reuse.
- `WrapFluidSurfaceOrigin` called before CPU scan.

## Flight

Warm AF still FAIL on holes/thrash — hitch helpers alone do not CLOSED stop-SLA (see A27_S2).
