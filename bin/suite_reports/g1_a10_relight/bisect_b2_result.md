# Bisect result B2 (manual `203535`)

## Build

Tip `7a25581b` + **A2 ON** (row frustum) + **A1 OFF** (pre-`58f65ffe` publish).  
Logs:
- `bin/logs/perf_20260914-203535_27472.jsonl`
- `bin/logs/enter_lit_20260914-203625.jsonl`

## Operator eye

Частичное восстановление vs tip `200927`: **редкие мигания чанков**, **чёрные чанки, которые постепенно уходят**. Texture-swap storm не описан.

## Mid-corridor telemetry (periods after enter)

| Metric | tip `200927` (A1 ON) | **B2 `203535` (A1 OFF)** | B1 `202741` (A2 OFF) |
|---|---:|---:|---:|
| unlit med | 12 | **6** | 36.5 |
| unlit ≥40 frac | 0% | 13% | 45% |
| dark_face_stale_near med | 43 | 78 | **160** (max 3514) |
| mesh_apply_stale_visual med | 3 | 4 | 21.5 |
| unfinished_visual med | 3 | **7** | 2.5 |
| visible_black_focus med | 70 | 75 | 69 |
| visible_black early→late | 84→62 | **91→63.5** | 63→78.5 |
| oldest_stale_VL age max | 40 | **212** | 16 |
| enter gate max ms | ~65 | ~160 | **~7958** |

Trends match eye: black debt **fades** late (B2), unlike B1 where stale **worsens**.

## Verdict

- **A1 is a contributor** to tip regress (eye better without atomic publish; swap storm not reported).
- **Not a full PASS**: residual chunk blink + fading blacks remain → not sole root.
- Keep **A1 OFF** for next step; do **not** restore A1 yet.
- Next: **B3** = revert A4 **lazy** `IsSpawnMeshRingReady` only (keep telem / GPU ms wipe / substages).

## Next

Checklist: `bisect_b3_no_a4_lazy_manual.md`
