# Creature — post-B: коллизии существ и камера

Краткое руководство после итерации B: entity–entity collision и три режима камеры (classic F5 cycle).

Каталог существ и скинов (палитра, spawn, `skin_id`): [CREATURE_CATALOG.md](CREATURE_CATALOG.md).

---

## 1. Entity collision

### Схема

```
ResolveMovement / ResolveMovementBody
  → ResolveMovementAxis* (Y, X, Z)
    → CheckCollisionVolume(vol, skipCreatureId)
         → CheckBlockCollisionVolume(vol)   // блоки, всегда
         → CheckCreatureCollisionVolume(vol, skip)  // если gameplay.entity_collision
```

- **`skipCreatureId`** — id движущегося существа; с ним не проверяется пересечение (не «врезается в себя»).
- **`skipCreatureId == 0`** — не пропускать никого (спавн, статические проверки).
- Игрок через камеру: `World::GetMovementCollisionSkipId()` → `controlledCreatureId_` (игрок или possess-моб).

### Конфиг

`config.json`:

```json
"gameplay": {
  "step_up": true,
  "entity_collision": true
}
```

- **`true` (default)** — AABB всех записей в `creatures_` участвуют в коллизии.
- **`false`** — только блоки (как до post-B).

### Ключевые файлы

| Файл | Роль |
|------|------|
| `World.cpp` | `CheckBlockCollisionVolume`, `CheckCreatureCollisionVolume`, axis-resolve |
| `Creature.cpp` | `ApplyIntent` → `ResolveMovementBody(..., id_)` |
| `Camera.cpp` | `ResolveMovement(..., GetMovementCollisionSkipId())` |

---

## 2. Camera (eye vs view rig)

| Слой | Поле / метод | Назначение |
|------|----------------|------------|
| Eye anchor | `Camera::Position` | Физика, коллизии, `UpdateIntersection`, sync с `Creature` |
| View rig | `ComputeCameraWorldPosition()` + `UpdatePose()` | `lookAt` с offset в 3rd person |
| Отрисовка | `GeometryEngine::RenderCreatures` | Скрыть **controlled** только в First Person |

### Три perspective (`CameraPerspective.h`)

| Режим | F5-цикл | Камера |
|-------|---------|--------|
| FirstPerson | старт | `Position` = eye |
| ThirdPersonBack | 2-й шаг | eye − Front × 4 + Up × 0.5 |
| ThirdPersonFront | 3-й шаг | eye + Front × 4 |

- **F5** — `Application::RouteKey` (в игре, консоль закрыта); HUD: `CameraPerspectiveLabel`.
- Gradient sky: **F6 / F8** (не F5).

### Possess

`controlledCreatureId_` = моб → в 3rd person виден wireframe моба; коллизии eye моба с телом игрока в `creatures_` работают.

---

## 3. Чеклист приёмки

- [ ] `entity_collision` default on; в config `false` → проход сквозь мобов
- [ ] Игрок упирается в `spawn_test_mob`
- [ ] Два моба не проходят друг через друга при wander
- [ ] F5: First person → Third back → Third front → First person
- [ ] 1st person: тело controlled не видно; 3rd: controlled виден
- [ ] `possess` + 3rd person показывает моба
- [ ] Регрессия: прыжок, step-up, полёт, hotbar, консоль `` ` ``

---

## Связанные документы

- [`CREATURE_IMPLEMENTATION.md`](CREATURE_IMPLEMENTATION.md) — полный план итерации B
- [`CREATURE_AGENTS.md`](CREATURE_AGENTS.md) — фаза агентов (позже)
