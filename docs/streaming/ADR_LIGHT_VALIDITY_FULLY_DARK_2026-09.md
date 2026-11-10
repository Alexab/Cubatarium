# ADR: FullyDark equal-rev remesh policy (A21 P0.5 / P4)

Date: 2026-09-22  
Status: Deferred to P4  
Context: `relight_install_planner_test` FAIL `P7: skip remesh when FullyDark light rev matches`

## Decision (interim)

Do **not** delete the failing assertion. Zero sky/block light is a legal result
(`LightValidity` ≠ dark vertex census). Until P4 separates value=0 / validity /
freshness / visibility, the planner may still treat dark faces as stale.

P4 will align planner + test: valid dark cave with matching light revision must
skip remesh; stale halo with matching center revision must reject/rebuild.

## Related

- CURRENT_STATE_AUDIT A21-07
- ENGINE_REMEDIATION_PLAN P4
