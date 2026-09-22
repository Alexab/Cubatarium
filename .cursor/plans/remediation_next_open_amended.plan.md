# Remediation next cycle — OPEN/AMENDED after A25 return

Date: 2026-09-22  
Parent: [A25_CONFORMANCE_AUDIT_2026-09-22.md](E:/Work/Home/Cubatarium/docs/streaming/A25_CONFORMANCE_AUDIT_2026-09-22.md)  
Canon: [ENGINE_REMEDIATION_PLAN_2026-09-22.md](E:/Work/Home/Cubatarium/docs/streaming/ENGINE_REMEDIATION_PLAN_2026-09-22.md)  
Baseline: current `cursor_audit7_impl` HEAD after A25 return (not `dcc02e27` TODO copy).

## Non-goals

- No A25+ FullyDark remesh / force_stale flood / RemoveChunk pending FD / shell-light mirror.
- No RingReadiness ON until eye + A24 stop-lines + stop-SLA.
- No reopen of CLOSED R0 honesty / A24 FREEZE / P1 land / P2 cutover structure.

## Open / AMENDED work (ordered)

### N1 — P2 liveness finish
- Randomized event suite (reorder/cancel/unload/reload, 2 Y, 6 neighbors, peer-ready-before-subscribe).
- Prove stop → Published or explicit permanent error; zero orphan pending / infinite Retain.
- Remove remaining dual setters if any remain under cutover ON.

### N2 — P3 publish e2e
- Inventory all CPU/GPU publish callers; wire `ValidatePublicationCandidate` with live epochs.
- Deferred retirement / fence ownership.
- Keep publication_audit green via ADR (stale draw rejection).

### N3 — P4 light + seam
- Slow CPU full light reference on small worlds; scenario suite vs incremental.
- Seam extract or temporary manifest-validating coverage (D3 shell mirror stays FREEZE).
- Finite converge after stop; end-debt gate only after holes-stable.

### N4 — P5 bounded MT
- Resumable Capture/shell/upload/fluid units.
- Unified admission pools; prove `dirty_dropped/period` ≤800 on warm AF+manual.

### N5 — P6 fluid
- Worker/continuation column summary rebuild.
- Tiled/toroidal surface map; water↔lava / world-switch tests.
- Hitch gate: no fluid_map tens/hundreds ms in flight.

### N6 — Acceptance
- Execute [A25_R6_EYE_PROTOCOL.md](E:/Work/Home/Cubatarium/docs/streaming/A25_R6_EYE_PROTOCOL.md); operator eye PASS.
- Frame A/B vs `dcc02e27`; then merge_green may become true.

### N7 — RingReadiness (only after N6)
- Enable per [A25_P7_RING_DEFERRED.md](E:/Work/Home/Cubatarium/docs/streaming/A25_P7_RING_DEFERRED.md) criteria.

### N8 — P8 profile (only after N6 + profile)
- Per [A21_P8_PROFILE_GATES.md](E:/Work/Home/Cubatarium/docs/streaming/A21_P8_PROFILE_GATES.md).

## Final of this next plan

Must again end with conformance vs ENGINE_REMEDIATION + successor plan for any remaining OPEN.
