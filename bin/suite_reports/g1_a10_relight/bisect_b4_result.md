# Bisect result B4 (manual `205048`) — FAIL

## Build

A2 ON + **A1 ON** (`58f65ffe`) + eager spawn ring (B3).  
Logs:
- `bin/logs/perf_20260914-205048_36464.jsonl`
- `bin/logs/enter_lit_20260914-205140.jsonl`

## Operator eye

- Дыры, подмена текстур, мигания — **вернулись в полном объёме** (как tip `200927`).
- Чёрные чанки **только на границе кольца**, исчезают до подлёта (лучше, чем «липкая вода» B3).

## Mid-corridor (focus `(7,3)→(−3,3)` like tip)

| Metric | B3 A1OFF | **B4 A1ON** | tip A1ON |
|---|---:|---:|---:|
| unlit med / ≥40 | 2 / 0% | **2 / 15%** (max 64) | 12 / 0% |
| mesh_apply_stale_visual med→late | 14 | **16 → late 30** | 3 → late 8 |
| missing_resident age max | 24 | **54** | 7 |
| black early→late | (opp. dir) | **95→55** | 92→61 |
| enter gate ms | ~7890* | **~170** | ~65 |

\*B3 enter spike likely start/dir artifact; B4 enter healthy.

Black fade early→late matches «только на кольце, уходят до подлёта».

## Verdict

- **A1 atomic chunk-pass publish = главный виновник** tip-regress (дыры / texture swap / мигания). Воспроизводится при A1 ON даже с eager ring.
- Чёрные на кольце при A1 ON — ожидаемый rim debt, не sticky mid-field.
- **Рабочая конфигурация bisect:** A2 ON + **A1 OFF** + eager ring (B3). N01 нужно пересаживать иначе (не текущий group-commit).

## Next

Restore A1 OFF (back to B3 tree) for continued eye / fix design. Do not claim G1 CLOSED.
