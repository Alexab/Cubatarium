# ADR: publication epoch vs order-only table updates (A21 P0.5)

Date: 2026-09-22  
Status: Accepted (interim; full epoch split lands in P3)  
Context: `publication_audit` FAIL `reordered_table_keeps_publication_epoch`

## Decision

Order-only membership reorder of resident batches **must not** bump artifact
`publicationVersion`. MDI / compact consumers use a separate **resident table
revision** (and transparent order key) that is invalidated on reorder.

The failing assertion in `publication_audit` expects artifact epoch change on
reorder. That expectation is wrong for the intentional production contract.
Until P3 lands full epoch split + stale-draw rejection tests:

- Keep production behavior (no artifact bump on order-only).
- Treat `publication_audit` order-epoch check as **contract conflict**, not
  dangling-memory regression.
- P3 will update the test to verify stale draw rejection via table/order epoch.

## Related

- `relight_install_planner_test` FAIL P7 FullyDark: deferred to P4 LightValidity
  ADR — do not delete the assertion.
