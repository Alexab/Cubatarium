# Remediation baseline metrics

> Recorded at remediation phase 0.2 (2026-07-04). Re-run after phase 6 to measure shrink.

## God-class LOC (source lines, approximate)

| File | Baseline LOC | Current LOC (2026-07-05) | Target after remediation | Status |
|------|--------------|--------------------------|--------------------------|--------|
| `src/World/Physics/FluidSpreadSystem.cpp` | 1293 | **183** | coordinator < 400; services < 500 each | **met** |
| `src/Render/Engine/GeometryEngine.cpp` | 2151 | **2012** | −300 via fog pass extract (phase 6) | partial (−139 vs baseline) |
| `src/World/Core/World.cpp` | 2100 | **2111** | incremental facade slices (phase 6) | partial (+11) |

## Phase 6/7 update (2026-07-04)

| File | Baseline LOC | Current LOC | Delta vs baseline |
|------|---------------|-------------|-------------------|
| `src/Render/Engine/GeometryEngine.cpp` | 2151 | 2265 | +114 |
| `src/World/Core/World.cpp` | 2100 | 2323 | +223 |

- `GeometryEngine` now routes underwater fog through `UUnderwaterFogPass`; boundary extracted, but baseline drift still kept file above phase-0 LOC at that checkpoint.
- `World.cpp` gained a fluid facade slice (`TryAddFluidObject`, `ApplyBreakSiteFluidFlood`) to isolate fluid policy paths from placement/break orchestration.

## Phase 8 DoD closure (2026-07-05)

| Item | Result |
|------|--------|
| `FluidSpreadSystem` split | coordinator 183 LOC; spread services in dedicated units |
| `audit_style.py` | **0 violations** (legacy creature prefixes whitelisted per TD-CRE-029) |
| Block JSON migration | `tools/migrate_block_fluid_presets.py` — **66** definitions updated (`fluid_kind` / `fluid_permeable`) |
| CI smoke | `fluid_surface_map_logic_test`, `worldgen_fluid_vegetation_pipeline_test`, `worldgen_hills_vegetation_gate_test` |
| QA runbook | [`QA_FLUIDS_2026.md`](QA_FLUIDS_2026.md) — automated PASS; manual fog band follow-up |

## Audit commands

```bash
python tools/audit/orchestrate.py --phase baseline
python tools/audit/orchestrate.py --phase scan
python tools/audit_style.py
python tools/migrate_block_fluid_presets.py --dry-run
```

## Open audit items (relevant)

- TD-AUD-010: UWorld god-class
- TD-AUD-011: UApplication god-class
- TD-AUD-012: GeometryEngine coupling (partial; LOC −139 vs baseline)
- TD-AUD-026: UCore god-class (partial)
- TD-AUD-027: World→Render headers (partial; RenderMeshSink wired, mesh cache in render path)

See [AUDIT_REPORT_2026.md](AUDIT_REPORT_2026.md) and [TECH_DEBT_AUDIT.md](TECH_DEBT_AUDIT.md).
