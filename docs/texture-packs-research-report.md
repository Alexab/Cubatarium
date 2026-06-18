# Отчёт: аудит текстур Cubatarium и сравнение бесплатных texture packs

Дата: 2026-06-17  
Данные анализа: `E:/Work/Home/CubatariumTextureResearch/analysis/raw/`  
Скрипты: [`tools/download_texture_packs.ps1`](../tools/download_texture_packs.ps1), [`tools/analyze_texture_packs.py`](../tools/analyze_texture_packs.py)

---

## 1. Executive summary

### Текущие текстуры

Текстуры в репозитории (`textures/blocks/`, 240 PNG) с высокой достоверностью являются **официальными текстурами Minecraft Java Edition 1.12.2** (старые имена вроде `hellrock`, `oreCoal`, `cloth_0`):

- **239 из 239** общих файлов **побайтово идентичны** внешней папке `E:/Work/Home/CubatariumTextures/blocks`
- **4 эталонных файла** (`dirt`, `stone`, `grass_side`, `oreCoal`) совпадают по MD5 с [minecraft-assets 1.12.2](https://github.com/InventivetalentDev/minecraft-assets)
- Лицензия на графику в репозитории **не указана**; код под MIT, текстуры — **запатентованные/лицензионные активы Mojang/Microsoft**

**Риск:** распространение игры с текущими текстурами без лицензии Mojang юридически небезопасно. Замена на CC0/CC-BY наборы — приоритет для открытого релиза.

### Что первично: блоки или текстуры?

**Первичны блоки.** Мир хранит `BlockId`, worldgen и сохранения ссылаются на имена блоков. Текстуры — слой отображения через `models/blocks/*.json` и stems в `textures/blocks/`. Новые текстуры без записи в манифесте не используются; новые блоки требуют ID, JSON и интеграции в worldgen/UI.

### Краткий вывод по замене

| Критерий | Лучший кандидат |
|----------|-----------------|
| Юридическая чистота (CC0) | Kenney Voxel Pack + Pattern Pixel (гибрид) |
| Покрытие sandbox-блоков | Minetest default (CC BY-SA 3.0), ~37% блоков полностью |
| Совместимость 16×16 без ресайза | Minetest default (230 из 241 PNG — 16×16) |
| Бесшовные паттерны (не блоки) | Kenney Pattern Pixel, Goncalo Pixel Patterns |

Ни один скачанный CC0-набор **не покрывает** все 162 блока Cubatarium без alias-map и доработки. Рекомендуется **многоуровневая система texture packs с fallback** (см. раздел 8).

### VectorPixelStar

Набор «VectorPixelStar CC0 64 textures» **не найден** на OpenGameArt/itch.io под этим именем. Вероятные источники путаницы: [Seamless Pattern Pack](https://opengameart.org/content/seamless-pattern-pack) (Ragnar Random, CC0) или коллекция [CC0 Textures](https://opengameart.org/content/cc0-textures) на OGA.

---

## 2. Текущий набор Cubatarium

| Параметр | Значение |
|----------|----------|
| PNG в репозитории | 240 |
| Блоков в манифестах | 162 |
| Уникальных texture stems | 193 |
| Лишние PNG (вне манифеста) | 47 (оверлеи, варианты, неиспользуемые) |
| Разрешение | **16×16** (235 файлов); анимации: `water` 16×512 (32 кадра), `lava` 16×320, `fire_*` 16×32 |
| Фильтр | `GL_NEAREST` |
| Импорт | [`tools/import_blocks.ps1`](../tools/import_blocks.ps1) из `CubatariumTextures/blocks` |
| Бесшовность (terrain-оценка) | 17.9% strict seamless, 42% likely+seamless среди квадратных тайлов |

### Именование (признаки Minecraft Java)

`oreCoal`, `hellrock`, `whiteStone`, `cloth_0..15`, `comparator_lit`, `tripWireSource`, `netherBrick`, `lightgem`, `musicBlock` — схема **Beta/1.5–1.7**, не Minetest/Kenney.

### Подтверждение происхождения

```
Репозиторий ←→ CubatariumTextures: 239/239 идентичны (MD5)
Репозиторий ←→ minecraft-assets 1.12.2:
  dirt.png      ✓ идентичен
  stone.png     ✓ идентичен
  grass_side.png ✓ идентичен
  oreCoal.png   ✓ идентичен (coal_ore в MC 1.12+)
```

Полные данные: `analysis/raw/origin_verification.json`

---

## 3. Скачанные наборы

Корневая папка: `E:/Work/Home/CubatariumTextureResearch/`

| ID папки | Лицензия | PNG | Доминирующий тайл | Stems 193 | Блоки full 162 | Seamless %* |
|----------|----------|-----|-------------------|-----------|----------------|-------------|
| `cubatarium_current` | Unknown (MC) | 240 | 16×16 | 100% | 100% | 17.9% |
| `kenney_voxel_pack` | CC0 | 197 | 128×128† | 24.4% | 26.5% | 72.2% |
| `kenney_pattern_pixel` | CC0 | 133 | 16×16 | 0% | 0% | 57.6% |
| `kenney_pattern_lines` | CC0 | 125 | 256×256 | 0% | 0% | 66.7% |
| `minetest_default` | CC BY-SA 3.0 | 241 | 16×16 | 34.7% | 37.0% | 33.1% |
| `seamless_pattern_pack` | CC0 | 432 | 64×64 | 11.9% | 14.2% | 7.7% |
| `oga_16x16_blocks` | CC0 | 2 | 256×256 | 0% | 0% | 0% |
| `oga_mc_inspired` | CC0 | 6 | 16×16 | 0% | 0% | 40.0% |
| `sbs_sandbox_terrain` | CC0 | 95 | 64×64 | 1.0% | 1.2% | 0% |
| `goncalo_pixel_patterns` | CC0 | 63 | 16×16 | 0.5% | 0.6% | 25.0% |
| `vexed_block_land` | CC0 | 3 | спрайтшиты | 0% | 0% | n/a |

\*Доля strict `seamless` среди квадратных тайлов (сравнение левого/правого и верхнего/нижнего краёв, порог ≤4 RGB на канал).  
†Kenney Voxel Pack: игровые тайлы в `PNG/Tiles/` — **128×128**, не 16×16; для стиля MC потребуется даунскейл или смена визуального стиля.

### Примечания по наборам

- **Kenney «Pixel Shading»** — отдельного пака с таким именем нет; использован **Pattern Pack Pixel** (ближайший аналог бесшовных паттернов).
- **Kenney Pattern Lines** — скачан как [Pattern Pack 2](https://kenney-assets.itch.io/pattern-pack-2) (линейные паттерны 256/512px), не block textures.
- **OGA 16×16 Block Set** — `blocks.zip` на OGA сейчас содержит только 1 PNG + `.blend`; `tilemap.png` — спрайтшит 128×128, не набор отдельных блоков.
- **Vexed Block Land** — zip содержит 3 PNG-спрайтшита для платформера, не кубические face-текстуры.
- **SBS Sandbox Terrain** — 96 текстур 64×64 (top/side по биомам); имена не совпадают с MC stems.

---

## 4. Покрытие по категориям блоков

### Kenney Voxel Pack — полное покрытие (43 блока)

`sand`, `dirt`, `gravel`, `glass`, `ice`, `snow`, `hellrock`, `hellsand`, `whiteStone`, руды/блоки redstone, каменные кирпичи, `cactus`, шерсть (через `cotton_*`), часть дерева (`tree_log` частично через `wood`), `sandstone_*`, `netherrack`/`soul_sand` аналоги.

**Не покрывает:** большинство руд (`oreCoal`, `oreIron`, …), металлические блоки, механизмы (рельсы, поршни, редстоун-логика), растения, двери, кровати, воду/лаву (анимация).

### Minetest default — полное покрытие (60 блоков)

`sand`, `dirt`, `gravel`, `glass`, `clay`, `obsidian`, `ice`, `snow`, `bookshelf`, `oreDiamond`, `oreRedstone`, металлы redstone/diamond, `sandstone`, `cobble`/`stone` варианты, дерево default, листва, вода/лава (другие имена файлов).

**Не покрывает:** `hellrock` (netherrack в MT — другое имя), шерсть 16 цветов, большинство MC-механизмов, `melon`, старые имена руд `oreCoal` → `default_stone_with_coal` (есть в MT, но alias нужен).

### Pattern packs (Kenney Pixel/Lines, Goncalo)

Абстрактные **бесшовные паттерны** — подходят для заполнения `dirt`/`stone`/`brick` при ручном маппинге, но **не содержат** боковую траву, двери, анимированную воду и т.д.

---

## 5. Варианты замены (для выбора)

### A. Kenney Voxel Pack (CC0)

| | |
|--|--|
| **Плюсы** | CC0, без атрибуции; узнаваемый voxel-стиль; 72% seamless; базовый terrain |
| **Минусы** | Тайлы 128×128; только 26.5% блоков; другой визуальный стиль; нет механизмов |
| **Интеграция** | Alias-map ~50 записей; даунскейл 128→16 или смена «пиксельного» стиля игры |
| **Оценка трудозатрат** | 2–4 дня (маппинг + тест визуала) |

### B. Minetest Game default (CC BY-SA 3.0)

| | |
|--|--|
| **Плюсы** | 16×16; лучшее покрытие (37% блоков, 35% stems); полноценный sandbox-набор |
| **Минусы** | CC BY-SA: атрибуция + ShareAlike на производные; другие имена (`default_*`) |
| **Интеграция** | Alias-map ~120–150 записей; `THIRD_PARTY_NOTICES.md` |
| **Оценка трудозатрат** | 3–5 дней |

### C. Гибрид CC0 (Kenney terrain + Pattern Pixel + SBS)

| | |
|--|--|
| **Плюсы** | Полностью CC0; гибкий визуал |
| **Минусы** | Нет единого стиля; всё равно нужны placeholder для ~60% блоков |
| **Интеграция** | Приоритетный pack + 2 fallback; ручной кураторский маппинг |
| **Оценка трудозатрат** | 1–2 недели |

### D. OGA 16×16 Block Set (CC0)

| | |
|--|--|
| **Плюсы** | CC0; задуман как minecraft-clone set |
| **Минусы** | Фактически недоступен (2 файла на OGA); 0% покрытия |
| **Вердикт** | **Не рекомендуется** в текущем виде |

### E. Постепенная миграция

| | |
|--|--|
| **Суть** | CC0 primary pack + placeholder (`missing.png`) + fallback на Minetest/Kenney по категориям |
| **Плюсы** | Можно выпускать инкрементально; снижает юридический риск сразу |
| **Минусы** | Временный «лоскутный» вид |
| **Оценка** | MVP texture resolver (раздел 8) + 1 primary CC0 pack |

---

## 6. Лицензионная матрица

| Лицензия | Коммерция | Атрибуция | ShareAlike | Совместимость с MIT-кодом |
|----------|-----------|-----------|------------|---------------------------|
| **CC0** | ✓ | нет | нет | ✓ идеально |
| **CC BY 3.0** | ✓ | ✓ | нет | ✓ (уведомления в CREDITS) |
| **CC BY-SA 3.0** | ✓ | ✓ | ✓ на производные | ⚠ SA распространяется на производные текстуры |
| **Minecraft default** | ✗ без лицензии Mojang | — | — | ✗ |

**Рекомендация для `THIRD_PARTY_NOTICES.md`:**

- Kenney packs: «CC0 1.0, optional credit Kenney.nl»
- Minetest: «CC BY-SA 3.0, Copyright celeron55/Perttu Ahola et al., modifications under same license»
- Текущие MC-текстуры: **удалить** из публичных сборок

---

## 7. Поддерживаемое разрешение в движке

Источник: [`src/Render/Textures/TextureCube.cpp`](../src/Render/Textures/TextureCube.cpp), [`BlockAtlasUV.h`](../src/World/Math/BlockAtlasUV.h)

| Параметр | Поведение |
|----------|-----------|
| Жёсткий лимит размера | **Нет** в коде — берётся из PNG |
| Практический стандарт | 16×16 (текущий набор) |
| Фильтр | `GL_NEAREST` — пиксель-арт |
| Анимация | Вертикальная полоса: `height = frame_count × width` |
| 6 граней блока | Одинаковый размер всех face-текстур **обязателен** |
| Разные размеры между блоками | **Разрешено** |
| Верхний предел | `GL_MAX_TEXTURE_SIZE` GPU (обычно 4096–16384) |
| VRAM (оценка) | 173 блока × 16×16×6 ≈ 1.6 MB RGBA raw atlases — мало; при 32×32 ≈ 6.4 MB |

**Вывод:** поддерживаются 16×16, 32×32 и выше. Для Kenney 128×128 — либо даунскейл при импорте, либо смена стиля (крупные пиксели). Анимации water/lava/fire требуют vertical strip в том же разрешении, что и статические тайлы.

---

## 8. Архитектура множественных texture packs

Сейчас: только build-time импорт через [`import_blocks.ps1`](../tools/import_blocks.ps1). Runtime-смены пака нет.

### Модель

```
config.json (texture_packs priority)
        ↓
TexturePackResolver
        ↓
canonical stems (193) → [primary pack] → [fallback 1] → [fallback 2] → missing.png
        ↓
TextureBaseStorage → TextureCubeStorage → GPU atlas
```

### Структура пака

```json
{
  "id": "kenney_voxel",
  "license": "CC0-1.0",
  "tile_size": 16,
  "aliases": { "dirt": "dirt", "grass_top": "grass_top", "oreCoal": "greystone_coal" },
  "face_rules": {
    "grass": { "sides": "dirt_grass", "top": "grass_top", "bottom": "dirt" }
  },
  "extras": ["crate", "barrel"]
}
```

### Автоматическое разрешение коллизий

| Коллизия | Решение |
|----------|---------|
| Stem в нескольких паках | Побеждает меньший `priority` |
| Stem отсутствует в primary | Fallback из следующего пака |
| Пак даёт только top/side | `face_rules` в `pack.json` |
| Разный `frame_count` анимации | Primary; fallback только если primary не имеет stem |
| Разное разрешение внутри блока | **Запретить** (валидатор) |
| Лишние текстуры пака | `extras` — не регистрировать как блоки без opt-in |

### Этапы внедрения

| Этап | Содержание |
|------|------------|
| **MVP** | `TexturePackResolver`, секция в `config.json`, 2 пака (CC0 primary + fallback), placeholder |
| **v2** | UI выбора пака, hot-reload |
| **v3** | `register_pack_extras.ps1`, валидатор seamless, авто-discovery |

### Файлы для MVP

| Файл | Изменение |
|------|-----------|
| `src/Render/Textures/TexturePackResolver.h/cpp` | новый |
| `src/Render/Textures/TextureBase.cpp` | цепочка fallback |
| `config.json.example` | `texture_packs: [...]` |
| `tools/import_blocks.ps1` | генерация alias-map |
| `docs/ARCHITECTURE.md` | описание системы |

### Пополнение блоков из новых текстур

Opt-in CLI `tools/register_pack_extras.ps1`: сканирует `extras`, предлагает новые `BlockId` — требует ручного approve (worldgen/UI не обновятся автоматически). **Блоки первичны** — новые текстуры не создают блоки без явного решения.

---

## 9. Рекомендуемые следующие шаги

1. **Принять решение** по варианту замены (A–E) с учётом лицензии и покрытия.
2. **Убрать MC-текстуры** из публичных веток / релизов.
3. Создать `THIRD_PARTY_NOTICES.md` для выбранных CC0/CC-BY-SA паков.
4. Реализовать **MVP TexturePackResolver** с Minetest или Kenney как primary + CC0 fallback.
5. Расширить `STEM_ALIASES` в `analyze_texture_packs.py` по мере маппинга.

---

## 10. Реализованные Luanti resource packs (2026-06-18)

Скачаны и собраны в `resource_packs/`:

| Pack ID | Блоков | Лицензия | Источник |
|---------|--------|----------|----------|
| `minetest_default_16` v2 | 261 | CC BY-SA 3.0 | [minetest-game/default](https://github.com/minetest-game/default) |
| `refi_textures_16` | 108 | CC BY-SA 4.0 | [MysticTempest/REFI_Textures](https://github.com/MysticTempest/REFI_Textures) |
| `programmer_art_16` | 127 | CC BY 4.0 | [deathcap/ProgrammerArt](https://github.com/deathcap/ProgrammerArt) |
| `snez_16` | 87 | CC BY-SA | [FrugalGamer/snez-texture-pack](https://github.com/FrugalGamer/snez-texture-pack) |
| `too_many_stones_16` | 38 | CC0 | [asuna-mt/Too_Many_Stones](https://github.com/asuna-mt/Too_Many_Stones) |

Пайплайн: `tools/download_texture_packs.py` → mapping YAML → `tools/build_research_resource_packs.py`.  
Полный Minetest: `tools/minetest_stem_map.yaml`, `tools/generate_minetest_upstream_catalog.py`, composite руд (`stone^mineral`).

---

## Приложение: команды

```powershell
# Скачать все паки
powershell -ExecutionPolicy Bypass -File tools/download_texture_packs.ps1

# Анализ
python tools/analyze_texture_packs.py

# Импорт блоков (текущий пайплайн)
powershell -ExecutionPolicy Bypass -File tools/import_blocks.ps1
```

Артефакты: `E:/Work/Home/CubatariumTextureResearch/analysis/raw/summary.json`, `block_coverage_matrix.csv`, `origin_verification.json`.
