# Bisect result B3 (manual `204326`)

## Build

A2 ON + **A1 OFF** + **A4 lazy OFF** (eager `IsSpawnMeshRingReady`).  
Logs:
- `bin/logs/perf_20260914-204326_14372.jsonl`
- `bin/logs/enter_lit_20260914-204423.jsonl`

## Operator eye

- Редкие мигания чанков **ушли** (лучше B2).
- **Чёрные чанки на воде в конце** остались.

## Direction note

B3 focus `(-3,3)→(7,3)` (обратно tip/B2 `(7,3)→(-3,3)`). Конец = зона `(7,3)`, witness `dark_face_block_id=377` @ y≈49 (тот же id в autofly wall-diet у воды).

## Mid-corridor telemetry

| Metric | tip `200927` | B2 `203535` | **B3 `204326`** |
|---|---:|---:|---:|
| unlit med / ≥40 | 12 / 0% | 6 / 13% | **2 / 0%** |
| unfinished med | 3 | 7 | **2.5** |
| mesh_apply_stale_visual | 3 | 4 | **14** |
| visible_black med | 70 | 75 | 77 |
| black stalled med | 5 | 8 | **34.5** |
| prep_spawn_ring_query_ms | 0 | 0 | **~25** |
| wall_ms med | 52 | 57 | **79** |
| enter gate max ms | ~65 | ~160 | **~7890** |

Late B3 @ `(7,3)`: black 79–96, unlit 10–19, `dark_id=377`. Tip **early** у `(7,3)` тоже black~92 — вода/377 не уникальна для A1 OFF.

## Verdict

- **A4 lazy** — вкладчик редких миганий → **оставить eager** (пока), несмотря на ~25 ms ring query и длинный enter.
- **Вода/чёрные в конце** — не закрыты B3; частично зона `(7,3)`/377 (есть и на tip).
- **A1 OFF** оставляем до B4: проверить, усиливает ли partial-publish водный black.

## Next — B4

Keep A2 + eager ring. **Restore A1** atomic publish. Eye: water blacks vs return of texture swap.
