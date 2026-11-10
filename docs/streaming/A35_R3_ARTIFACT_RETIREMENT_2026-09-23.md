# A35 R3 — ArtifactManifest retirement status

Date: 2026-09-23  
Parent: [A30_V3_P3_P6.md](A30_V3_P3_P6.md), [A34_CONFORMANCE](A34_CONFORMANCE_2026-09-23.md)

## Decision

Full retirement of dual ArtifactManifest / legacy publish paths is **deferred** until P2.2 pre-pub is stable on clean SHA after A35 SourceMismatch fix (no parallel hang work).

## Progress this realign

| Caller | Status |
|---|---|
| CPU Apply / Immediate / GPU Commit | ValidatePublicationCandidate before mutate; A34 dark carve-out; A35 self-consistent stamps |
| Cross / shell | `ValidateCrossOrShellPublication` on PublishRevs identity (no MeshedLight lag storm) |
| Dual header vs live desire | Still open — desire mismatch remains SoftDefer/MarkDirty, not SourceMismatch |

## Do not

- SoftDefer heal / PreferKick / Ring ON to “finish” retirement
- Close Gate 8 or merge_green on this doc alone
