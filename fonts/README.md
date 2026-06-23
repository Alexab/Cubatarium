# Шрифты для Cubatarium

В дистрибутив (Windows installer, Android APK/AAB) входит один открытый UI-шрифт.

## Текущий шрифт

| Файл | Лицензия | Назначение |
|------|----------|------------|
| `Roboto-Regular.ttf` | [Apache 2.0](LICENSE-Roboto.txt) | Основной UI-шрифт (меню, HUD, отладочный текст) |

Источник: [googlefonts/roboto](https://github.com/googlefonts/roboto) (`src/hinted/Roboto-Regular.ttf`).

Код ищет шрифт по пути `fonts/Roboto-Regular.ttf` (константа `kBundledUiFontRelPath` в `src/App/Platform/GameAssets.h`). При сборке папка `fonts/` копируется в runtime (`CMakeLists.txt`, Android `syncAssets`, Windows installer).

## Замена или добавление шрифта

1. Положите `.ttf` / `.otf` в эту папку.
2. Обновите `kBundledUiFontFileName` и `kBundledUiFontRelPath` в `GameAssets.h`, если меняете основной шрифт.
3. Добавьте файл лицензии (`LICENSE-*.txt`) и запись в [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md).

Используйте только шрифты с лицензией, разрешающей распространение (Apache 2.0, OFL, CC0 и т.п.). **Не добавляйте** проприетарные системные шрифты (Arial, Calibri, Segoe UI).

## Повторная загрузка Roboto

```powershell
curl.exe -fsSL -o fonts/Roboto-Regular.ttf `
  "https://github.com/googlefonts/roboto/raw/main/src/hinted/Roboto-Regular.ttf"
```
