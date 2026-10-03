# A27 S4 — Seam product path

Date: 2026-09-22

## Landed

- `PeerReadyBeforeSubscribe` gates FaceDebt clear on lit/coverage commit in `ChunkEmergeCoordinator`.
- BecameKnown neighbor remesh requires publisher peer-ready before subscribe/coalesce.
- `SeamCoverageFullySatisfied` / `ShouldCommitSeamCoverage` remain contract APIs (manifest FREEZE).

## FREEZE kept

- D3 shell-light mirror not revived.
- No force_stale flood / RemoveChunk pending FullyDark.

## Status

**PARTIAL** — product seam extract beyond FaceDebt peer gate still OPEN for full temporary coverage commit path.
