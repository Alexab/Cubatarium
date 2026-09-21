# Sysreset operator + AF gates (v6 vs SoT 145008)

Controls vs manual `bin/logs/perf_20260921-145008_25236.jsonl` (v5 remesh regress).
Prior baselines: `112357` (v4), `101105` (pre-v4).
Docs: [SYSRESET_V6_AUDIT_145008.md](SYSRESET_V6_AUDIT_145008.md),
[SYSRESET_V5_AF_EVIDENCE.md](SYSRESET_V5_AF_EVIDENCE.md).

**Warning: AF ≠ manual.** v5 AF cold VB fly 22 while manual 145008 stayed ~45.
Do not claim CLOSED on AF-only VB wins.

## AF suite (fog ON)

```bash
set CUBA_FLIGHT_FOG_ON=1
python -X utf8 tools/flight_sim_run.py --world World_164 --scenario product-174657
python -X utf8 tools/flight_sim_run.py --world World_164 --scenario product-174657 --warmup-sec 20
python -X utf8 tools/flight_sim_run.py --world World_164 --scenario product-174657-dive
python -X utf8 tools/n01_v21_scorecard.py <perf.jsonl> -o bin/suite_reports/g1_a10_relight/<label>_score.json --label <label>
```

## Scorecard gates (v6 vs 145008)

| Gate | 145008 bad | Pass |
|---|---|---|
| west_route_coverage | COVERED | COVERED (cx≤−3) |
| mesh_apply_stale_visual mid med | 8 / eye Δ 1 | **≤1** mid med |
| dark_face_stale_near fly max | **498** | ≪498 (Δ↓≥50%) |
| VB fly med | 45 | ≤45 and trending ↓ (not AF-only CLOSED) |
| VB stalled fly med | 6 | ≤6 / toward ≤7.5 |
| unfinished max | 64–70 | ≤70 trending ↓ |
| dirty_fm | — | not above 145008 fly without kick blow-up |
| PreferKick / FD ForceDirty | PreferKick≡0 | ForceDirty path live / PreferKick when pending |
| prior_lit_hold fly med | — | ≤400 KEEP |
| flip / dual | 0\|0 | KEEP 0\|0 |
| mesh_gpu_kick_ms max | — | ≤20 KEEP |
| mesh_emerge fly max | — | ≤40 KEEP |
| render_total / cull skip | hitch C | KEEP |
| eye-proxy | PASS | PASS |
| operator blacks+sky+dark faces | OPEN | CLOSED only after **manual** eye |

## Anti-regress KEEP

Focus admit bypass MarkRelit; FD ForceDirty-no-pending; no SoftDefer-for-holes;
Unknown always-hide; no PreferKick without pending; hitch C untouched;
no FaceDebt on light Accept (v3 D4); FaceDebt SoftDefer peer skip + cap2.

## Operator SoT

| Class | Check |
|---|---|
| black FullyDark chunks | VB fly ≤45 trend ↓; stalled ≤6 |
| sky-through faces | unfinished ≤70 trend ↓; holes≈0 |
| **black block faces** | stale_visual≤1; dark_face_stale_near ≪498 |
| hitch C | opaque cull skip / transparent resort KEEP |

Artifacts: `bin/suite_reports/g1_a10_relight/sysreset_v6_*`.
Evidence: [SYSRESET_V6_AF_EVIDENCE.md](SYSRESET_V6_AF_EVIDENCE.md).
