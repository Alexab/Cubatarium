# Era14 Postmortem (1f52bdd5..HEAD → V4)

> Date: 2026-08-07  
> Evidence: `manual_latest_151212.json`, `perf_20260807-151212_27980.jsonl`  
> Plan: Era14 V4 refactor (frame extract + DesiredStage + commit seed)

## One-line diagnosis

Throughput and Hide⇒Ticket contracts exist, but heal admission is wall-gated
(`Imm`/`stale-wave` need calm `last_frame_ms`) while streaming/mesh still nest
inside `DoMovement` — so wall never calms during flight and chunk tops stay
missing.

## KEEP

| Item | Why |
|------|-----|
| Hide⇒Ticket / ColumnFlow | Era13 SoT repair ownership |
| ColumnRenderable SoT / AllowUnlitFirstMesh | Draw truth; SoftDefer contract |
| FirstMesh ≠ Remesh | Admission floors |
| DigSeam FIFO | Edit seam path |
| MeshWorkAdmission floors | Throughput SoT (not rim zoo) |
| F0 `sync_cap=0` on hot frames | Avoid SyncRebuild hitch |
| cy0 Dirty admit / DigSeam below place | Invisible tops partial fix |
| Drain-first GPU under backlog | Apply progress |
| ARCH_D3_LAND harness | Land tops not caught by ocean cruise |

## DISCARD (do not repeat)

| Pattern | Evidence |
|---------|----------|
| Calm-wall Imm as primary FirstMesh (`last_frame_ms≤40`) | `stand_rim_imm_n=0` on 151212 |
| Stand vs cruise sticky Imm fork | Zoo; Inflight thrash history |
| Wall≤50 on stale-wave **enqueue** | `stale_repair_wave_n=0` under miss |
| Fly-wide sync / global SyncRebuild fill | Wall spikes |
| Cap `mesh_schedule` on holes | Async underfeed |
| Fog / draw-hide as throughput | Masks unfinished |
| SoftDefer zoo packages across subsystems | 104145-class |
| MemoryBudget thrash as rim SLA | Residency only |
| Ocean-only validation for land tops | 142306 / 151212 |
| Close phase without autofly gate + TD update | Process |

## Target execution (Era14 V4)

1. **Phase 1** — `TickWorldStreamingPhase` outside `RunLegacyPhysicsFrame`.
2. **Phase 2** — DesiredStage; kill calm Imm primary; stale enqueue without wall gate.
3. **Phase 3** — Strengthen commit-time seed; remesh-on-lit after UnlitFirstMesh.
4. **Phase 4** — Worker Capture residual; trim duplicate IdleRecovery knobs.
5. **Phase 5** — PREMERGE rejects + full autofly matrix.

TD tracking: `TECH_DEBT_CHUNK_STREAMING.md` TD-ARCH-040..048.
