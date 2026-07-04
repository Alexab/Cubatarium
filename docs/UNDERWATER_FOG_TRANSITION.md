# Подводный туман при погружении: анализ и план доработки

> Связано: [FLUID_ARCHITECTURE.md](FLUID_ARCHITECTURE.md), [TECH_DEBT_FLUIDS.md](TECH_DEBT_FLUIDS.md) (TD-FL-029),  
> код: `World.cpp` (`FindFluidColumnSurface`, `IsCameraInsideFluid`), `GeometryEngine.cpp`, `fshader_greedy.glsl`.

## 1. Проблема

При **погружении** на короткий момент видно **дно / подводную геометрию без водяного тумана** — с distance fog вместо underwater fog.

Это не «дыра» в mesh / отсутствие верхней грани воды. Симптом: **подводные блоки рисуются с наземным освещением и туманом**, пока камера формально ещё не считается «под водой».

### Текущее поведение (ветка `water`, variant A implemented)

| Режим | Условие | Эффект |
|-------|---------|--------|
| Полный подводный туман | `eye.y < BlockTopY` верхнего liquid в колонке глаз | Global fog + snap цвета при входе (без изменений) |
| Below-surface tint | `!cameraInFluid` + `fragment.y < surfaceY(fragment.xz)` | Per-column tint через `UFluidSurfaceMap` (water/lava) |
| Стояние в мелкой воде, голова над поверхностью | `eye.y >= surface` | Full-screen fog **выкл**; tint на блоках ниже surface своей колонки |

Константы **не привязаны к pitch/FOV**; зависят от **вертикали** `eye.y` и эмпирической полосы. Смена `eyeHeight` капсулы может потребовать retune полосы.

---

## 2. Как эту проблему описывают в индустрии

Известные названия: **«underwater sight»**, **«hover at waterline»**, **«frame without underwater effects when camera moves fast»**.

Суть: камера **ещё формально над поверхностью** (или на границе), а в кадре уже видна **подводная геометрия** с **наземным** fog/lighting.

В гайдах по terrain/water прямо пишут: *«even AAA games tend to just live with it»* — если камеру поставить у поверхности, можно «рентгенить» дно с настройками воздуха ([terrain.chriskempke.com/water](https://terrain.chriskempke.com/water/)). Oblivion, простые Unity-подводные сцены — типичные примеры в обсуждениях ([Unity Discussions](https://discussions.unity.com/t/smooth-underwater-transition/432569), [gamedev.se #58026](https://gamedev.stackexchange.com/questions/58026/how-to-prevent-underwater-sight-in-games)).

---

## 3. Подходы в 3D (от простого к «правильному»)

| Уровень | Подход | Примеры | Плюсы | Минусы |
|--------|--------|---------|-------|--------|
| **1** | Бинарный toggle: `camera.y < waterLevel` → global fog / post-process | Unity-туториалы, простые indie | Дёшево, предсказуемо | Pop при быстром нырянии; «рентген» у поверхности |
| **2** | Post-process **volume** + **blend radius** у границы объёма | UE4/UE5 ([Versluis](https://www.versluis.com/2020/10/adding-a-post-process-volume-for-a-simple-underwater-effect-in-unreal-engine/), [Epic forums](https://forums.unrealengine.com/t/underwater-fog-half-in-and-half-out/299106)) | Плавный вход в объём | Весь экран внутри volume; половина экрана «над/под водой» **не решается** без кастомного PP |
| **3** | **Per-pixel / per-fragment**: fog только если **мировая Y фрагмента < поверхности воды** в его колонке | [gamedev.se #58026](https://gamedev.stackexchange.com/questions/58026/how-to-prevent-underwater-sight-in-games), Catlike Coding [Looking Through Water](https://catlikecoding.com/unity/tutorials/flow/looking-through-water/) | Камера **над** водой + взгляд вниз; не зависит от pitch | Нужна поверхность на каждый `(x,z)`; depth reconstruct или tint в terrain shader |
| **4** | **Post-process**: reconstruct world pos из depth, split по waterline | Crysis-подобные решения | Корректная «половина экрана под водой» | Дорого: второй pass/camera, depth ([terrain guide](https://terrain.chriskempke.com/water/)) |
| **5** | **Trigger/collider** воды + PP (не height alone) | Subnautica-style ([Terresquall](https://blog.terresquall.com/community/topic/underwater-effect-shader-from-the-subnautica-series/)) | Стабильно в известном объёме | Привязка к коллайдеру; движение water body — отдельная боль |
| **6** | **Transition fog** при входе (густой → обычный за N сек) | Minecraft Java ([Fog wiki](https://minecraft.wiki/w/Fog)) | Маскирует pop при погружении | Не убирает «рентген» у поверхности; отдельная система blend |

### Minecraft (близкий жанр)

- Подводный fog — **когда камера в воде** (бинарно по объёму).
- При **спуске** — **transition fog**: очень густой на ~0.01 блока, blend 25% → 60% @ 5s → off @ 30s.
- В shaderpacks — per-fragment depth/absorption при взгляде **сквозь** воду.

---

## 4. Наше решение сегодня — место на шкале

**Гибрид уровня 1 + урезанного уровня 3:**

1. **Полный подводный туман** — бинарно `eye.y < BlockTopY` (уровень 1). Без grace/body-boost (корректно для wading).

2. **Pre-submerge tint** — per-fragment в `fshader_greedy`: `vWorldPos.y < uFluidSurfaceY`, но:
   - включение только в полосе **20 см** над поверхностью + `body.inFluid`;
   - `surfaceY` из **колонки камеры**, не из `(fragment.x, fragment.z)`;
   - pitch/FOV не участвуют; константы подобраны эмпирически.

3. **Snap** цвета тумана/неба при первом кадре полного погружения (дух Minecraft transition fog, но мгновенный).

| Критерий | Best practice (tier 3–4) | Сейчас |
|----------|--------------------------|--------|
| Камера над водой, взгляд вниз на дно | Tint по **Y фрагмента** vs surface в **его** колонке | Tint только в полосе 20 см + surface из колонки **глаз** |
| Камера под водой | Global underwater fog | ✅ |
| Половина экрана на/под водой | Custom PP + waterline | ❌ (не целимся) |
| FOV / pitch | Не зависит | ✅ |
| `eyeHeight` | Параметрическая привязка | ⚠️ Полоса 0.20 фиксирована |
| Стоимость | PP + depth дороже | Дёшево: 2 uniform + строки в greedy shader |

**Итог:** tier **1.5–2** — осознанный компромисс для voxel forward renderer без post-process pipeline.

---

## 5. Выводы: дорабатывать ли дальше?

**После `e58e9fb` — срочной необходимости нет**, если визуально устраивает.

- Проблема **частично неизбежна** без дорогого waterline-split; индustry часто **живёт с компромиссом**.
- Дальнейший tuning `0.20 / 0.72 / 0.52` — **убывающая отдача**.
- Архитектурный скачок (PP + depth, 2 cameras) **не окупится** на текущем этапе Cubatarium.

**Вернуться к доработке**, если:

- меняется **eyeHeight / stance** и flash возвращается;
- появляются **озёра на разной высоте** (одна `surfaceY` из колонки глаз ломается);
- нужно **убрать магические константы** без полного PP.

---

## 6. Вариант A (рекомендуемый следующий шаг)

### 6.1 Идея

**Tier 3 в чистом виде для opaque terrain (greedy mesh):**

- **Full-screen underwater fog** — без изменений: только при `eye.y < surfaceY(колонка глаз)`.
- **Below-surface tint** — для **каждого фрагмента**, если  
  `fragment.y < surfaceY(fragment.x, fragment.z)`  
  **без** полосы 20 см и **без** `body.inFluid` как обязательного условия.
- Tint **не** затрагивает небо и блоки **выше** поверхности в своей колонке → wading с горизонтальным взглядом не получает full-screen fog.

Это соответствует рекомендации gamedev.se: *«activate fog only if that point is under the water surface»* — но в world-space Y, не через reconstruct near plane.

### 6.2 Non-goals

- Split-screen waterline (Crysis), второй camera, post-process pipeline.
- Изменение правила wading (full-screen fog только под `BlockTopY`).
- Tier 6 Minecraft 30s transition fog (опционально позже как polish).

### 6.3 Ключевое отличие от текущего кода

| | Сейчас | Вариант A |
|---|--------|-----------|
| Условие включения tint | `body.inFluid` + полоса 20 см над surface | `!cameraInFluid` + есть карта поверхностей |
| Surface Y | Одна `uFluidSurfaceY` (колонка глаз) | **Per-column** lookup по `fragment.xz` |
| Шейдер | `vWorldPos.y < uFluidSurfaceY` | `vWorldPos.y < surfaceYAt(vWorldPos.xz)` |
| Константы band | `0.20`, `0.72` | **Удалить** |

---

## 7. План реализации варианта A — **implemented**

Реализовано: `FindFluidColumnSurfaceAt` (`FluidColumnSurfaceQuery`), `FluidSurfaceColumnSlice` + кэш в `UChunkMeshCache`, `UFluidSurfaceMap`, шейдер `surfaceYAt`/`fluidIndexAt`, `FluidViewProfile::BelowSurfaceFogMin/Scale`, тесты `underwater_fog_column_test` / `fluid_surface_slice_test`. Эвристика 20 cm band удалена.

### Фаза 0 — рефакторинг запроса колонки (низкий риск)

**Цель:** единая функция поверхности для любой `(bx, bz)`.

**Файлы:** `World.h`, `World.cpp`

1. Выделить из `FindFluidColumnSurface(eye)`:
   ```cpp
   FluidColumnSurface FindFluidColumnSurfaceAt(int bx, int bz, int hintY);
   ```
   - Скан сверху вниз: `y` от `hintY + 8` до `hintY - 64` (или до `world minY`).
   - Первый `IsLiquid` + `BlockRenderStyle::Fluid` → `surfaceY = BlockTopY(y)`.
   - Игнорировать waterlogged permeable (как сейчас для full-screen fog).

2. `FindFluidColumnSurface(eye)` → thin wrapper: `WorldCoordToBlockIndex(eye.x/z)`, hint = `WorldCoordToBlockIndex(eye.y)`.

3. Unit-тест (можно в новом `underwater_fog_test.cpp` или расширить существующий):
   - колонка с водой на y=63, air выше → surface 63.5;
   - колонка без liquid → `valid=false`;
   - два уровня воды не нужны — верхний liquid wins.

**Критерий готовности:** поведение `IsCameraInsideFluid` **не меняется**.

---

### Фаза 1 — кэш поверхности per chunk (CPU)

**Цель:** не сканировать мир per-frame per-column O(N²).

**Новые типы:** `src/Render/Mesh/FluidSurfaceColumnMap.h` (или рядом с `ChunkMeshSnapshot`)

```cpp
// 16×16 int16 на chunk: BlockTopY верхнего liquid или kNoSurface.
struct ChunkFluidSurfaceSlice {
  static constexpr int16_t kNoSurface = INT16_MIN;
  int16_t surfaceY[CHUNK_SIZE][CHUNK_SIZE]; // world block Y of top liquid block
  BlockId fluidId[CHUNK_SIZE][CHUNK_SIZE];  // optional, для цвета lava/water
};
```

**Построение:**

- При `ChunkMeshSnapshot::Capture` / rebuild mesh / `MarkBlockChunkDirty` — пересчитать slice для затронутого chunk (один проход 16×16×scan Y).
- Альтернатива v1: lazy build при первом запросе в кадре (проще, но возможен hitch — лучше при mesh build).

**Инвалидация:** тот же `meshRevision` / chunk dirty, что и greedy mesh.

**Критерий готовности:** для chunk с морем на SeaLevel slice совпадает с `FindFluidColumnSurfaceAt` spot-check.

---

### Фаза 2 — GPU height map вокруг камеры

**Цель:** шейдер читает surface Y по world XZ.

**Новый класс:** `UFluidSurfaceHeightTexture` в `src/Render/Engine/`

| Параметр | Значение (начальное) |
|----------|----------------------|
| Размер | `(2 * renderDistChunks + 3) * CHUNK_SIZE` по X и Z, выровнено по chunk |
| Формат | `GL_R16F` или `GL_R32F` (world Y surface; `-1000` = нет воды) |
| Центр | block XZ камеры, округление к chunk |
| Обновление | Каждый кадр в `PrepareFrameRendering` **или** при изменении `meshRevision` / смежении центра на ≥8 блоков |

**Upload pipeline:**

1. Для каждого loaded ground-chunk в окне — скопировать `ChunkFluidSurfaceSlice` в staging buffer.
2. `glTexSubImage2D` в регион texture.
3. Uniforms:
   - `uFluidSurfaceMap` (sampler2D)
   - `uFluidSurfaceOrigin` (vec2) — world XZ угла texel (0,0)
   - `uFluidSurfaceTexelSize` (vec2) — 1/size
   - `uBelowSurfaceFog` — 1.0 когда `!cameraInFluid`, иначе 0 (full fog уже работает)
   - `uBelowSurfaceFogColor`, strength params из `FluidViewProfile`

**Fallback:** если texture не готова — поведение как сейчас (single `uFluidSurfaceY` из колонки глаз) или tint off.

**Критерий готовности:** debug overlay (опционально) показывает height map; визуально tint на дне при взгляде вниз **без** полосы 20 см.

---

### Фаза 3 — шейдер

**Файлы:** `shaders/fshader_greedy.glsl`, `shaders/gles/fshader_greedy.glsl`

```glsl
float surfaceYAt(vec2 worldXZ) {
    vec2 uv = (worldXZ - uFluidSurfaceOrigin) * uFluidSurfaceTexelSize;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
        return 1e9; // вне карты — не tint
    float h = texture(uFluidSurfaceMap, uv).r;
    if (h < -500.0) return 1e9;
    return h;
}

// После sample albedo, до distance fog:
if (uBelowSurfaceFog > 0.001) {
    float surfaceY = surfaceYAt(vWorldPos.xz);
    if (vWorldPos.y < surfaceY) {
        float depthBelow = surfaceY - vWorldPos.y;
        float factor = uBelowSurfaceFog * clamp(
            uBelowSurfaceFogMin + depthBelow * uBelowSurfaceFogScale,
            uBelowSurfaceFogMin, 1.0);
        FragColor.rgb = mix(FragColor.rgb, uBelowSurfaceFogColor, factor);
    }
}
```

**Параметры `Min/Scale`** — вынести в `FluidViewProfile` (закрыть tech debt с hardcoded `0.52/0.35`).

**Прозрачная вода (greedy transparent pass):** v1 — **не трогать** (opaque seafloor уже tint); v2 — optional absorption в fluid shader.

**Creatures / cross:** вне scope v1; при необходимости — те же uniforms в их шейдерах.

---

### Фаза 4 — удаление эвристики и cleanup

**Файлы:** `GeometryEngine.cpp`, `GeometryEngine.h`

- Удалить `kPreSubmergeFragmentFogBand`, ramp `0.72…1.0`, `FluidSurfaceY` single-column uniform.
- `BelowSurfaceFogStrength = cameraInFluid ? 0.f : 1.f` (при валидной height map).
- Оставить `enteringUnderwater` hard snap для full-screen fog.

**Документация:** обновить [ARCHITECTURE.md](ARCHITECTURE.md) (раздел fog), закрыть TD-FL-029.

---

### Фаза 5 — тесты и QA

| Тест | Ожидание |
|------|----------|
| Открытое море, ныряние вертикально | Дно tint **до** `eye.y < surface`; full fog сразу после |
| Мелкая вода, стоя, горизонт | Full-screen fog **выкл** |
| Мелкая вода, взгляд вниз на дно | Дно может быть tint (физически корректно); обсудить с дизайном |
| Озеро выше/ниже SeaLevel | Surface из slice, не из sea level constant |
| Лава | `fluidId` → свой `FogColor` в slice (если реализован per-column color) |
| Chunk unload at map edge | Вне карты — нет артеfact tint (sentinel) |

**Manual QA:** добавить пункт в [PHYSICS_ROLLOUT.md](PHYSICS_ROLLOUT.md) §fluids view.

---

## 8. Оценка трудозатрат и рисков

| Фаза | Сложность | Риск |
|------|-----------|------|
| 0 — refactor query | S | Низкий |
| 1 — chunk slice cache | M | Средний (sync с dirty) |
| 2 — GPU height map | M | Средний (edge chunks, upload budget) |
| 3 — shader | S | Низкий |
| 4 — cleanup | S | Низкий |
| 5 — tests/QA | S | — |

**Порядок:** 0 → 1 → 3 (с CPU lookup в shader stub / uniform grid на 1 chunk для dev) → 2 → 4 → 5.

**Производительность:** slice 16×16×~80 Y × N chunks при rebuild — дёшево; upload R16F ~ (renderDist×32)² × 2 byte/кадр — приемлемо.

---

## 9. Альтернативы, если height map окажется тяжёлой

1. **Coarser map** — 1 texel = 1 block, half resolution.
2. **Только окно 64×64** вокруг камеры (не весь render distance).
3. **On-shader loop** — не подходит для forward greedy (нет world access).

---

## 10. Связанный tech debt

| ID | Описание |
|----|----------|
| TD-FL-029 | Per-column below-surface fog (variant A) — см. §7 |

---

## История

| Дата | Коммит / событие |
|------|------------------|
| 2026-07 | `f093eab` — pre-submerge tint (колонка глаз + band) |
| 2026-07 | `e58e9fb` — tune band 20 cm, ramp, shader strength |
| 2026-07 | Этот документ — анализ + план variant A |
