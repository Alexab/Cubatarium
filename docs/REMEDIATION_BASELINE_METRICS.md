# Remediation baseline metrics

> Recorded at remediation phase 0.2 (2026-07-04). Re-run after phase 6 to measure shrink.

## God-class LOC (source lines, approximate)

| File | LOC | Target after remediation |
|------|-----|--------------------------|
| `src/World/Physics/FluidSpreadSystem.cpp` | 1293 | coordinator < 400; services < 500 each |
| `src/Render/Engine/GeometryEngine.cpp` | 2151 | −300 via fog pass extract (phase 6) |
| `src/World/Core/World.cpp` | 2100 | incremental facade slices (phase 6) |

## Audit commands

```bash
python tools/audit/orchestrate.py --phase baseline
python tools/audit/orchestrate.py --phase scan
python tools/audit_style.py
```

## Open audit items (relevant)

- TD-AUD-010: UWorld god-class
- TD-AUD-011: UApplication god-class
- TD-AUD-012: GeometryEngine coupling (partial)
- TD-AUD-026: UCore god-class (partial)
- TD-AUD-027: World→Render headers (partial)

See [AUDIT_REPORT_2026.md](AUDIT_REPORT_2026.md) and [TECH_DEBT_AUDIT.md](TECH_DEBT_AUDIT.md).
