# Operator + AF gates (ownership cutover vs SoT 161139)

Controls vs manual `bin/logs/perf_20260921-161139_28636.jsonl`.
Black-clean anchors: `27beca1c`, `dd7871ab` (v3 end).
Docs: [OWNERSHIP_CUTOVER_AUDIT_161139.md](OWNERSHIP_CUTOVER_AUDIT_161139.md).

**AF ≠ manual.** CLOSED only after operator eye.

## AF suite (fog ON)

```bash
set CUBA_FLIGHT_FOG_ON=1
python -X utf8 tools/flight_sim_run.py --world World_164 --scenario product-174657
python -X utf8 tools/flight_sim_run.py --world World_164 --scenario product-174657 --warmup-sec 20
python -X utf8 tools/flight_sim_run.py --world World_164 --scenario product-174657-dive
python -X utf8 tools/n01_v21_scorecard.py <perf.jsonl> -o bin/suite_reports/g1_a10_relight/<label>_score.json --label <label>
```

## Scorecard gates vs 161139

| Gate | 161139 bad | Pass |
|---|---|---|
| west | COVERED | COVERED |
| unfinished max | 67 | ≤101105 class (≤63) trend ↓ |
| SoftDefer empty max | 200 | ≪200 / no sky-from-SoftDefer |
| VB fly / late spike | mid~51 late spike | ≤45; late no FullyDark plateau |
| PreferKick | ≡0 | >0 only with progress∧pending; ForceDirtyStuck KEEP |
| dark_face_stale max | 103 | ≤103 KEEP (v6) |
| stale_visual mid | — | ≤1 |
| wall_ms early max | 153 | ≪153 |
| opaque_cull_skipped | 1 | hitch C KEEP |
| kick / emerge / prior_lit | — | KEEP |
| flip/dual | 0 | KEEP |
| operator sky+blacks+UI | OPEN | CLOSED after **manual** eye |

## Anti-regress KEEP

v3 PreferKick∧progress; v5 focus admit + ForceDirtyStuck; SoftDefer≠Unknown;
no FaceDebt Dirty from mask; hitch C; D4; v6 dark reject; AF≠manual honesty.

Artifacts: `bin/suite_reports/g1_a10_relight/ownership_*`.
Evidence: [OWNERSHIP_CUTOVER_AF_EVIDENCE.md](OWNERSHIP_CUTOVER_AF_EVIDENCE.md).
