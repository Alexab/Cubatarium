# Cubatarium tools

Python/shell helpers for CI, content validation, and local diagnostics.

## CI / release gates

| Script | Purpose |
|--------|---------|
| `doctor-windows.ps1` | Build + core tests (Windows) |
| `integration_test_worldgen.py` | Worldgen metrics vs `worldgen_baseline.json` |
| `smoke_resource_packs.py` | Resource pack merge + audit smoke |
| `smoke_worldgen_metrics.py` | `movement_diagnostics.v2` budgets |
| `chunk_load_priority_test` | Chunk scheduler ordering (native binary) |
| `audit/ orchestrate.py` | Regenerate audit scan artifacts + report |

## Generators (authoring)

| Script | Purpose |
|--------|---------|
| `rebuild_release_resource_packs.py` | Rebuild release pack artifacts |
| `generate_prefab_features.py` | YAML manifest → `content/prefab_features.json` |
| `migrate_to_resource_pack.ps1` | Legacy block import → pack layout |

One-off migration scripts live in `tools/archive/` (see `archive/README.md`).

## Audit pipeline (`tools/audit/`)

Run full scan: `python tools/audit/orchestrate.py --phase all`

Scanners write JSON under `audit/`; `merge_findings.py` produces `docs/AUDIT_REPORT_2026.md`.

## Manual-only diagnostics

Not wired in CI — run locally when investigating:

- `profile_worldgen.py`, `debug_worldgen_seed.py`
- `validate_resource_pack.py` (also invoked by smoke)
- `scan_tools_usage.py` (orphan tool detection)

See `docs/CODING_STYLE.md` § Tools.
