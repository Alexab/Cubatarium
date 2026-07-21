# Implementation Plan

## Цель

Устранить ситуации, когда при движении в новые области:

- колонка не имеет mesh;
- mesh есть, но колонка остаётся тёмной;
- `pending_light_focus` застревает после остановки;
- autofly и manual дают разные выводы о готовности мира.

## Целевой Контракт

```mermaid
stateDiagram-v2
  [*] --> Empty
  Empty --> VoxelsReady
  VoxelsReady --> Lighting
  Lighting --> LitReady
  LitReady --> Meshing
  Meshing --> RenderReady
```

Правила:

1. Draw разрешён только для `RenderReady`.
2. `visual_holes` = отсутствующий mesh.
3. `unfinished_visual` = отсутствующий mesh или ещё неготовый свет.
4. `pending_light_focus` должен уходить к нулю в stop recovery.

## Фаза 0. Документация

Артефакты:

- `README.md`
- `EVOLUTION.md`
- `ARCHITECTURE_OPTIONS.md`
- `BEST_PRACTICES.md`
- этот файл

## Фаза 1. Strict Visual Contract

Изменения:

- добавить `IsColumnRenderReady`;
- разделить `visual_holes` и `unfinished_visual`;
- убрать first-mesh preview для колонок с `PendingLight`;
- перестроить flight-sim gates вокруг нового поля.

Критерий:

- stop segment не содержит тёмных preview chunks;
- `unfinished_visual` становится главным показателем незавершённого мира.

## Фаза 2. Focus Drain

Изменения:

- централизовать drain pending/render-ready cleanup в одном helper;
- при idle делать priority drain вокруг focus ring;
- не завязывать recovery только на raw `visual_holes`.

Критерий:

- `pending_light_focus` не стоит на месте при хорошем `wall_ms`;
- stop plateau сокращается.

## Фаза 3. Commit-Time Skylight Seed

Изменения:

- near-focus commit сначала пробует быстрый skylight seed, если соседний ring
  уже загружен;
- async relight остаётся fallback path;
- `PendingLight` используется только если seed небезопасен или недостаточен.

Критерий:

- уменьшается медиана `pending_light_focus`;
- уменьшается доля `unfinished_visual` в stop segment.

## Фаза 4. Unified Column Flow

Изменения:

- общий helper/flow для focus column work;
- снижение прямой связности между отдельными watchdog branches;
- постепенное сворачивание recovery zoo к единому scheduler ownership.

Критерий:

- для одной и той же колонки меньше дублирующих recover paths;
- легче читать и расширять scheduler.

## Фаза 5. Harness Parity

Изменения:

- `flight_sim_run.py --replay-manual`;
- анализатор понимает `unfinished_visual`;
- gates stop/fly привязаны к новому render contract.

Критерий:

- replay manual route и обычный fly-stop меряют одну и ту же проблему;
- отчёт различает `missing mesh` и `render unfinished`.

## Anti-Patterns

Не возвращаться к следующим решениям:

- `empty mesh == hole` глобально;
- `MarkDirty` всего focus ring на каждом idle tick;
- sync relight flood как универсальный recovery;
- preview mesh до завершения света.
