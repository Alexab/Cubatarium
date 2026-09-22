# A21 P2.7 demand cutover + rollback

## Flags

| Flag | Default | Role |
|---|---|---|
| `kChunkDemandShadow` | `true` | Observe + coalesce AlreadySatisfied; adapters still write Dirty/FaceDebt |
| `ChunkDemandCutoverEnabled()` | `false` | When true: **forbid** column `ClearFaceDebt` on lit/first-drawable; per-chunk `NoteFaceDebtSatisfied` is the writer |

## Cutover protocol

1. Shadow green on cold+warm+dive AF (input adequacy).
2. Enable `ChunkDemandCutoverEnabled() = true` for one AF trio.
3. Gate: no orphan pending / infinite Retain; Ready only from slice aggregate.
4. Evidence note + deletion of dual column clears behind the same flag.

## Rollback

1. Set `ChunkDemandCutoverEnabled() = false` immediately.
2. Do **not** leave cutover ON while re-enabling column clears (dual-path ban).
3. Keep demand store + fixtures; do not delete shadow diagnostics.

## Peer generation FaceDebt

`NoteFaceDebt(chunk, mask, peer_gen)` / `NoteFaceDebtSatisfied(..., peer_gen)`:
clear only matching `waiting_peer_gen[face]` (stale peer commits keep debt).
