# A33 — conformance (A34 empty-world hot-fix)

Date: 2026-09-23  
Parent: [A32_CONFORMANCE_2026-09-23.md](A32_CONFORMANCE_2026-09-23.md)  
Hot-fix: A34 empty-world / stuck MeshWarmup (dark→LightInvalid)

## P0 empty-world (operator + AF)

**Symptom:** MeshWarmup stuck ~36–50% (“Building meshes…”), then world opens empty (creatures only).

**Root:** A32 S3 mapped observational `hasFullyDarkFace` → `got.light_valid=false` with `expected.light_valid=true` → reject + remesh storm; enter exit does not require `opaque_cmd_on`.

**Fix:** Clear observational dark from ArtifactManifest LightInvalid (`ClearObservationalDarkFromLightValid`); lit→dark remains MeshLitGate / SoftDefer. Cross no longer fail-all on dark sample. Flight hard FAIL: `empty_world_stop_line` when `opaque_cmd_on_med==0` (min early-period 0 is diagnostic only).

| Gate | Status |
|---|---|
| Opaque presence after enter/AF | **REQUIRED** — `opaque_cmd_on_med > 0` |
| A34 cold AF (`bin/suite_reports/a34_empty/`) | `opaque_cmd_on_med=0.5` (was 0.0 on S0) — empty-world med gate PASS; other gates may still FAIL |
| Ring/profile | **OFF** until holes=0 + eye PASS |

## Finding matrix (post A34)

| ID | Status | Notes |
|---|---|---|
| Empty world / first-publish | **FIXED (code + AF med)** | cold `opaque_cmd_on_med=0.5` vs S0 `0.0`; Ring OFF |
| A31-01 seam | PARTIAL | unchanged |
| A31-02 fluid | PARTIAL | fluid_map spikes may remain |
| A31-03 attribution | PARTIAL | |
| A31-05/06 demand/pub | PARTIAL | provenance SourceMismatch kept; dark≠LightInvalid |
| A31-08 holes/eye | OPEN | not this hot-fix DoD |
| Ring | OFF | |

## Successor

**Superseded by A35 realign:** [A34_CONFORMANCE_2026-09-23.md](A34_CONFORMANCE_2026-09-23.md) + [.cursor/plans/remediation_after_a32_open.plan.md](../../.cursor/plans/remediation_after_a32_open.plan.md) (points back to A30/A31 P0–P7).
Do **not** treat A34 cold opaque_med>0 as A31 Gate 8 PASS.
