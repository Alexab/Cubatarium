# Audit 2026-09-16 S9 closure matrix

Source audit: commit `7a658525` / `docs/streaming/CURRENT_STATE_AUDIT_2026-09-16.md`.

## Absolute gates (must be green)

| Gate | Owner step | Status |
|---|---|---|
| live/free allocation overlap = 0 | S1 `publication_audit` | code + CTest |
| empty Replace / Remove last material | S2 | code + CTest |
| reorder bumps table identity | S2 | code + CTest |
| packed excluded when MDI resident | S2 GeometryEngine | code |
| eye-proxy absolute holes + missing fields | S0 | GateRepro |
| `operator_visual=None` ↛ merge_green | S0 scorecard | code |
| west route `cx≤−3` or UNTESTED | S0 | metrics field |
| per-frame GPU kick/finish timers | S3 | code |
| source light stamp on publish | S4 | code |
| per-column stalled remesh bookkeeping | S5 | code |
| draw-oracle age not reset on unrelated lit | S5 | CTest |
| snapshot credit lifetime with Store | S6 | code |
| spawn-ring cache world epoch + focus.y | S7 | code |
| no independent 4ms GPU floors | S7 | code |
| cull stats buffer-update barrier | S8 | code |

## Product acceptance (still OPEN without manual eye)

- Gate of record: `product-174657` **no-teleport** west.
- Autofly adequacy ≠ CLOSED.
- Manual operator west: mid blacks + texture-swap vs SoT `074859` / `123828`.
- Proposed SLA profile (not measured achievement): 16.7 ms or 33.3 ms frame;
  streaming slice ~5 ms; edit→visible p95≤100 / max≤250.

## KEEP

N01 incomplete=0 class, LegalDark rollback, frustum N02, cooldown N06,
I3t hold-prior (temporary until Replace/Remove convergence proven in field).

## FREEZE

N04 census FullyDark remesh caps / wrong-tex gates on misnamed `pass_mdi_stale_*`.
