# A36 S6 / S7 — attributed fix + acceptance

## S6 attributed class (manual 185333)

**Primary:** hinterland unfinished / not_ready with empty dirty starve (GeometryMissing lag), secondary GeometryCulled.

**Fix:** `UWorld::KickUnfinishedVisualRemesh(4)` — only when `UnfinishedVisual>0` and `GetDirtyCount()==0`. Not SoftDefer/PreferKick/force-stale.

Precision (A31-04) not implicated on west-272 route.

## S7 acceptance

| Check | Status |
|---|---|
| ≥5 A/B clean SHA | OPEN — one clean cold visible AF landed (`a36/cold.json`); need ≥5 + warm/far |
| holes=0 whole-route | FAIL — A24 `near_focus_holes_periods_gt0=4` on cold AF |
| post_stop PASS | OPEN |
| operator eye | UNTESTED |
| dirty-drop ≤800 | PASS on this AF (dirty_dropped med 219) |
| empty/enter stop-lines | PASS (`opaque_cmd_on_med=59`) |
| Ring | **OFF** |

Evidence: `bin/suite_reports/a36/cold.json`, perf `perf_20260923-195935_31488.jsonl` (SHA `2eda4362`, dirty=clean).

`merge_green=false` until Gate 8.
