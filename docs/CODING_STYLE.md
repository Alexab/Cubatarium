# Coding Style — Cubatarium `src/`

Эталон: [`src/App/Platform/WindowManager.h`](../src/App/Platform/WindowManager.h) и [`.clang-format`](../.clang-format).

## Именование

| Категория | Правило | Пример |
|---|---|---|
| Конкретный класс | префикс `U` | `UWindowManager` |
| Интерфейс | префикс `I` | `IPlatformWindow` |
| Enum / enum class | без префикса | `AppState`, `KeyCode` |
| Имя файла | **не менять** при U-rename | `WindowManager.h` |
| Член класса | PascalCase, без `_` | `WindowWidth`, `TouchBridge` |
| Метод | PascalCase | `SetInstances`, `PollEvents` |
| Поле struct | PascalCase | `SeaLevel`, `GuiRect::W` |
| Локальная / аргумент | snake_case | `last_ui_width` |
| `#include` проекта | от корня `src/` | `"Gui/Widgets/GuiButton.h"` |

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
python tools/audit/orchestrate.py --phase baseline
python tools/audit/orchestrate.py --phase scan
python tools/refactor_style.py --classes
python tools/refactor_style.py --members
python tools/fix_struct_fields.py
python tools/fix_bare_includes.py
python tools/fix_includes.py
python tools/fix_member_collisions.py
```

CI (Windows smoke workflow) also runs `python tools/audit_style.py` and `chunk_load_priority_test`.

## Render Pipeline includes

See [`src/Render/Pipeline/README.md`](../src/Render/Pipeline/README.md): Pipeline code must not `#include` `GeometryEngine.h`, `Gui/*`, or `Application.h`.

После массовых переименований: `clang-format` на `src/**/*.h` и `src/**/*.cpp` (кроме `ThirdParty/stb_image.h`).
