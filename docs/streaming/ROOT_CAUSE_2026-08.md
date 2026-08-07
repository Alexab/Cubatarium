# Root Cause: Era14 frame nest + wall-gated heal (2026-08)

## Одна фраза

Streaming/mesh всё ещё внутри `DoMovement`/`RunLegacyPhysicsFrame`, а rim heal
требует calm wall — при полёте wall 150–500+ мс, поэтому Imm/stale-wave не
стреляют и верхушки чанков остаются missing.

## Доказательство (manual_151212)

| Симптом | Метрика | Root |
|---------|---------|------|
| FPS ~1 в движении | wall max ~957, med ~196 | emerge+stream hitch |
| phys 170+ | phys med ~172, `physics_block≈0` | nest: phys/do_movement ⊃ emerge |
| Invisible tops | miss_cy 0–3 sticky ~38s | FirstMesh Dirty без Imm escape |
| Heal idle | `stale_repair_wave_n=0`, `stand_rim_imm_n=0` | gates `wall≤40/50`, `async<12/24` |

Логи: `bin/logs/perf_20260807-151212_27980.jsonl`,
`bin/iter_reports/manual_latest_151212.json`.

## Почему локальные rim-фиксы не закрывают SLA

1. Stand Imm / stale-wave admission зависят от `last_frame_ms` того же кадра, где
   emerge уже сжёг бюджет.
2. F0 убивает SyncRebuild на hot без отдельного FirstMesh escape (Dirty floor
   есть, но backlog не догоняет FOV).
3. Stand/cruise Imm forks вернули recovery zoo поверх ColumnFlow.
4. Land tops (`ARCH_D3_LAND`) не покрываются ocean teleport-cruise.

## Целевой контракт (Era14)

1. Frame: locomotion → **WorldStreamingPhase** → BlockInput → DigSeam → Render.
2. `phys_ms` / `do_movement_ms` не включают stream/emerge.
3. DesiredStage: miss/stale/void ⇒ ticket+Dirty **без** wall gate на enqueue.
4. Imm только edit/DigSeam/budgeted underfeet sync cost.
5. Commit-time seed + remesh-on-lit после UnlitFirstMesh.

См. [`ERA14_POSTMORTEM.md`](ERA14_POSTMORTEM.md), TD-ARCH-040..048.
