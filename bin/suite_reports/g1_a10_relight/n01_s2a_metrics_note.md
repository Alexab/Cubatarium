# S2a metric split (no remesh policy)

After N01 rework land, JSONL periods export:

| Field | Meaning |
|---|---|
| `stale_vl_rev_n` | Revision-mismatch stale only (`DrawOracleStaleVlRevN`) |
| `fully_dark_census_n` | FullyDark repair+no_ticket+stalled (`DrawOracleFullyDarkDebtN`) |
| `draw_oracle_stale_vertex_light_n` | Legacy census bridge (still folds FullyDark→StaleVL) |

`source=cpu_census` — not pixel truth (audit N04). Baseline mid-stalled SoT remains
manual `205626` until operator re-flies with this build.

Policy change (dark-face alone ≠ StaleVL demand) is S2b in the same land.
