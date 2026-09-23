# A36 S6 / S7 — attributed fix + acceptance

## S6 attributed class (manual 185333)

**Primary:** hinterland unfinished / not_ready with empty dirty starve (GeometryMissing lag), secondary GeometryCulled.

**Fix:** `UWorld::KickUnfinishedVisualRemesh(4)` — only when `UnfinishedVisual>0` and `GetDirtyCount()==0`. Not SoftDefer/PreferKick/force-stale.

Precision (A31-04) not implicated on west-272 route.

## S7 acceptance

| Check | Status |
|---|---|
| ≥5 A/B clean SHA | OPEN — run with `--visible`, no dirty tree |
| holes=0 whole-route | OPEN |
| post_stop PASS | OPEN |
| operator eye | UNTESTED |
| dirty-drop ≤800 | OPEN |
| Ring | **OFF** |

`merge_green=false` until Gate 8.
