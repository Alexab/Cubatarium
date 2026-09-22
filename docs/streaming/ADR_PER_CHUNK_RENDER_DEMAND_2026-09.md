# ADR: Per-chunk render demand owner (A21 P2)

Date: 2026-09-22  
Status: Accepted (shadow → cutover)  
Context: A21-03 / A21-04 / A21-06 — column-level progress and FaceDebt cannot own per-slice obligations.

## Decision

Each chunkXYZ has one **render demand record** that is the single logical owner of
desired vs published mesh/light work. Dirty / RAA / GPU pending remain adapters
to existing executors until cutover; they must not invent independent demand.

### Record fields (logical)

| Field | Role |
|---|---|
| `desired` | Latest required `(geom_rev, light_rev, coverage_gen)` |
| `activeAttempt` | At most one in-flight attempt (`attempt_id` + `JobStage`) |
| `queuedLatest` | Coalesced desire waiting for admission (bounded per chunk) |
| `published` | Last successfully installed `(geom_rev, light_rev)` |
| `waitingDependencies` | Optional peer coverage gens (6 faces) |

### Install / completion results

| Result | Meaning |
|---|---|
| `Published` | Candidate installed; published revs advance; demand may clear if desired met |
| `RetainedAwaitingSuccessor` | Prior drawable kept; **desired successor demand must remain** |
| `RejectedRetryable` | Candidate rejected; demand stays; may re-admit |
| `CancelledSuperseded` | Attempt cancelled; newer desire or orphan recovery owns the next step |

### Job stages (monotonic attempt lifetime)

`created → admitted → started → built → uploaded → published → retired`

Cancel may jump to `cancelled` from any pre-published stage. Stage advance is
real progress; PreferKick / stall counters alone are not.

### Demand note results

- `NewDemand` — desire raised or first recorded; enqueue allowed
- `Coalesced` — same/newer desire while attempt or queue already owns the chunk; enqueue may still hit legacy MarkDirty in shadow
- `AlreadySatisfied` — published meets desired; **must not** admit Dirty or bump progress

### Shadow → cutover

P2 lands the store in **shadow**: observe admit/install, coalesce AlreadySatisfied
skips, count mismatches. Cutover removes duplicate column progress setters and
makes this record the sole writer of demand.

## Related

- `ChunkRenderDemand.h`
- `ADR_SEAM_COVERAGE_CONTRACT_2026-09.md`
- Engine remediation plan P2
