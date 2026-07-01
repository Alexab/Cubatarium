# Coding Style — Cubatarium `src/`

Эталон: [`src/App/Platform/WindowManager.h`](../src/App/Platform/WindowManager.h) и [`.clang-format`](../.clang-format).

## Именование

| Категория | Правило | Пример |
|---|---|---|
| Конкретный класс | префикс `U` | `UWindowManager` |
| Интерфейс | префикс **`IU`** (`I` + `U`) | `IUWorldMeshSink`, `IUGameContent` |
| Enum / enum class | без префикса | `AppState`, `KeyCode` |
| Имя файла (класс `U*`) | **не менять** при U-rename | `WindowManager.h` |
| Имя файла (интерфейс `IU*`) | совпадает с типом | `IUWorldMeshSink.h` |
| Член класса | PascalCase, без `_` | `WindowWidth`, `TouchBridge` |
| Метод | PascalCase | `SetInstances`, `PollEvents` |
| Поле struct | PascalCase | `SeaLevel`, `GuiRect::W` |
| Локальная / аргумент | snake_case | `last_ui_width` |
| `#include` проекта | от корня `src/` | `"Gui/Widgets/GuiButton.h"` |

### Интерфейсы (`IU*`)

Интерфейс = `I` (interface) + `U` (общий префикс классов) + `Имя` → `IU<Имя>`.

- Все интерфейсы — только `IU*` (`IUPlatformWindow`, `IUGameContent`, `IUWorldMeshSink`, …).
- Имя файла совпадает с типом: `IUWorldPerception.h` → `class IUWorldPerception`.

## Форматирование

- Отступ: 2 пробела
- Скобки: Allman (открывающая на новой строке)
- `namespace cutum {` — скобка на той же строке

## Исключения

- `Version.h` — generated, путь без модуля
- `ThirdParty/stb_image.h`
- Android glue вне `src/`: `egl_context.h`, `android_jni.h`, `android_soft_keyboard.h`
- Внешние типы: `GLFWwindow`, `android_app`, `EglContext`

## Инструменты

```bash
python tools/audit_style.py
python tools/audit_clang_format.py
python tools/audit/check_include_rules.py
python tools/audit/orchestrate.py --phase baseline
python tools/audit/orchestrate.py --phase scan
python tools/refactor_style.py --classes
python tools/refactor_style.py --members
python tools/fix_struct_fields.py
python tools/fix_bare_includes.py
python tools/fix_includes.py
python tools/fix_member_collisions.py
```

CI (Windows smoke workflow) also runs `python tools/audit_style.py`, `python tools/audit_clang_format.py`, `python tools/audit/check_include_rules.py`, `audit_creature_catalog.py`, `audit_creature_backends.py`, `chunk_load_priority_test`, `creature_activity_steering_test`, and `navigation_pathfinder_test`.

## Tools (manual-only scripts)

Scripts under `tools/` that are **not** in CI — run locally when debugging:

| Script | When to use |
|--------|-------------|
| `profile_worldgen.py` | Deep worldgen timing / column stats |
| `debug_worldgen_seed.py` | Inspect a single seed layout |
| `validate_resource_pack.py` | Pack JSON/texture audit (also used by smoke) |

CI gates and generators are listed in [`tools/README.md`](../tools/README.md). One-off migrations live in `tools/archive/`.

## Render Pipeline includes

See [`src/Render/Pipeline/README.md`](../src/Render/Pipeline/README.md): Pipeline code must not `#include` `GeometryEngine.h`, `Gui/*`, or `Application.h`.

После массовых переименований: `clang-format` на `src/**/*.h` и `src/**/*.cpp` (кроме `ThirdParty/stb_image.h`).
