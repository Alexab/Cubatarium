---
name: remediation after A32 OPEN
overview: "A35 realign: resume A31/A30 P0–P7. A34 empty-world ≠ Gate 8. Ring OFF. Successor = a31_realign / remediation_after_a30."
todos:
  - id: r0-stabilize
    content: "A35 R0 SourceMismatch+enter dirty residual+fluid join; clean SHA AF"
    status: completed
  - id: r1-conformance
    content: "A34_CONFORMANCE Gate matrix honest; enter_dirty_residual stop-line"
    status: completed
  - id: p0-p1-evidence
    content: "Clean visible AF + ClassifyChunkDefect / freeze class (Gate 1–3)"
    status: pending
  - id: p2-p4-contracts
    content: "StopConverged + seam negatives + fluid install hang-safe evidence"
    status: pending
  - id: p5-p6-accept
    content: "Attributed fix + holes=0/eye/post_stop; Ring OFF until then"
    status: pending
  - id: residual-p3
    content: "Finish ArtifactManifest retirement after P2.2 stable"
    status: pending
isProject: false
---

# Remediation after A32 — realign to A31

Date: 2026-09-23  
Parent: [`docs/streaming/A34_CONFORMANCE_2026-09-23.md`](../../docs/streaming/A34_CONFORMANCE_2026-09-23.md)  
Canonical work order: [`remediation_after_a30_open.plan.md`](remediation_after_a30_open.plan.md)

## Binding stop-lines

- `opaque_cmd_on_med == 0` → FAIL (`empty_world_stop_line`)
- MeshWarmup timeout with `mesh_dirty` residual → FAIL (`enter_dirty_residual_stop_line`)
- No Ring ON until holes=0 + operator eye PASS
- No SoftDefer/PreferKick heal, force_stale, pending-FD RemoveChunk
- A32 S0 / A34 cold AF ≠ A31 Gate 8 PASS

## Ordered remainders (A31 P0→P7)

1. P0 clean visible AF + full manifest (far checkpoints)
2. P1 freeze + one `ChunkDefectClass` per white/partial sample
3. P2 demand StopConverged + pre-pub (A34 dark carve-out kept)
4. P3 seam AF negatives
5. P4 fluid Installed + no main-thread height×16×16
6. P5 attributed class fix only
7. P6 holes=0 / eye / post_stop
8. P7 Ring stays OFF
9. R3 ArtifactManifest retirement after P2.2 stable
