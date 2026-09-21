# Sysreset operator + AF gates (v5 vs SoT 112357 / 101105)

Controls vs manual `bin/logs/perf_20260921-112357_29496.jsonl` (v4 regress) and
`bin/logs/perf_20260921-101105_14028.jsonl` (pre-v4 SoT).
Docs: [SYSRESET_V5_AUDIT_112357.md](SYSRESET_V5_AUDIT_112357.md),
[SYSRESET_V4_AF_EVIDENCE.md](SYSRESET_V4_AF_EVIDENCE.md).

## AF suite (fog ON)

```bash
set CUBA_FLIGHT_FOG_ON=1
python -X utf8 tools/flight_sim_run.py --world World_164 --scenario product-174657
python -X utf8 tools/flight_sim_run.py --world World_164 --scenario product-174657 --warmup-sec 20
python -X utf8 tools/flight_sim_run.py --world World_164 --scenario product-174657-dive
python -X utf8 tools/n01_v21_scorecard.py <perf.jsonl> -o bin/suite_reports/g1_a10_relight/<label>_score.json --label <label>
```

## Scorecard gates (v5)

| Gate | 112357 bad | Pass |
|---|---|---|
| west_route_coverage | COVERED | COVERED (cx≤−3) |
| VB stalled fly med | 12.5 | ≤7.5 (≤101105) |
| VB fly med | 44.5 | ≤44.5 and trending ↓ |
| unfinished max | 79 | ≪79 / toward ≤48 |
| PreferKick / FD ForceDirty | PreferKick≡0 | PreferKick>0 when pending **or** ForceDirty path live |
| dirty_fm | 57 | not above 112357 without kick blow-up |
| prior_lit_hold fly med | ~8 | ≤400 KEEP |
| flip / dual | 0\|0 | KEEP 0\|0 |
| mesh_gpu_kick_ms max | ≪2 | ≤20 KEEP |
| mesh_emerge fly max | ~5–7 | ≤40 KEEP |
| render_total / cull skip | hitch C KEEP | KEEP |
| eye-proxy | PASS | PASS |
| operator sky / blacks | OPEN | CLOSED (manual eye) |

## Anti-regress KEEP

Focus admit bypass (not drop focus); no SoftDefer-for-holes; Unknown always-hide;
no PreferKick without pending; hitch C untouched; no FaceDebt on light Accept.

## Operator SoT

| Class | Check |
|---|---|
| black FullyDark | focus Dirty + FD ForceDirty; VB stalled ≤7.5 |
| sky-through faces | FaceDebt→Dirty already-known peer; unfinished↓ |
| hitch C | opaque cull skip / transparent resort KEEP |

Artifacts: `bin/suite_reports/g1_a10_relight/sysreset_v5_*`.
