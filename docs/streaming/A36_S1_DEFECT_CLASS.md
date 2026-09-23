# A36 S1 — Defect class samples (post-A35 manual)

Date: 2026-09-23  
Source: `perf_20260923-185333_32664.jsonl` + hang `171410`.

## Samples

| Sample | Evidence | Primary `ChunkDefectClass` |
|---|---|---|
| Hang 171410 | SourceMismatch×50k, opaque 18–21, MeshWarmup timeout | GeometryMissing (publish starve) — **fixed A35** |
| Manual 185333 mid-fly | unfinished_visual med 62, nfh periods=0, opaque med 50.5, vb med 49 | GeometryMissing hinterland / not_ready lag (not empty-world) |
| Manual 185333 cull | OpaqueCmdTotal≫On (med ~557 vs ~50) | GeometryCulled secondary — `NoteCullDecision` now logs focus span |

## Instrumentation landed

- `UJobStageTrace::NoteCullDecision` when `ChunkMeshedCulled0>0`
- `ClassifyChunkDefect` unit coverage (MeshPublishContractTest)

## Gate

Freeze-frame pixel ID still UNTESTED (operator). Class above is telemetry-attributed for S6 — not Gate 8 PASS.
