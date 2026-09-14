# Confirm flight `205626` — B3 config residual (sticky mid blacks)

## Build / logs

Working tree: **A2 ON + A1 OFF + eager spawn ring** (post-B4 restore).  
- `bin/logs/perf_20260914-205626_1296.jsonl`
- `bin/logs/enter_lit_20260914-205722.jsonl`

## Operator eye

- Дыры / texture swap / мигания **нет** (только чёрные).
- Чёрные **в середине** пролёта; **не проясняются** при пролёте мимо.

## Telemetry vs eye

| | tip `200927` | B4 A1ON FAIL | **B3b `205626`** |
|---|---:|---:|---:|
| unlit ≥40 | 0% | **15%** | **0%** (max 8) |
| near_focus_holes | ~0 | ~0–1 | **0** |
| black early→mid→late | 92→70→61 | **95→78→55** (rim fade) | 76→**68**→81 |
| stalled mid med | **~0** | ~32 | **~36** |
| oracle ≈ stale VL | yes | yes | **yes (debt=staleVL)** |

Mid third `(2,4)→(5,4)`: black 62–72 **не падает** при сближении; witness `dark_id` 378/560/572, dist у 572 падает 15→7 при focus `(5,4)` — чёрный остаётся под ногами. `stalled≈repair≈30–40`: repair крутится, но **не закрывает** fully-dark / stale vertex light.

Enter ~8.4 s снова (eastbound start) — отдельный cost eager/ring, не mid-black.

## Verdict

- Bisect win **подтверждён**: A1 OFF снимает дыры/swap/blink.
- Остаток = **sticky fully-dark + stale vertex light mid-corridor** (не mesh hole, не A1). Tip mid имел stalled≈0 — регресс repair/lighting при A1 OFF + eager, либо вскрытый старый light debt.
- Дальше копать: `visible_black_fully_dark_stalled` / stale VL apply на mid focus, witness 378/560/572 — **не** возвращать A1 group-commit.

Не G1 CLOSED.
