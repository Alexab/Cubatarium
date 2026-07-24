# F-Droid (черновик)

Подготовка к возможной публикации в [F-Droid](https://f-droid.org/). **MR в fdroiddata не входит в текущий scope.**

## Статус

| Критерий | Статус |
|----------|--------|
| Лицензия кода (MIT) | OK — см. [`LICENSE`](../../../LICENSE) |
| Сборка из исходников | Черновик рецепта: [`.fdroid.yml`](.fdroid.yml) |
| Google proprietary libs | Нет |
| Ассеты CC0 / CC BY-SA | OK с атрибуцией — [`THIRD_PARTY_NOTICES.md`](../../../THIRD_PARTY_NOTICES.md) |
| Воспроизводимая сборка | **Блокер:** FetchContent без pinned tags в `CMakeLists.txt` |
| Лицензии creatures | **Блокер:** часть `models/creatures/*/LICENSE.txt` — placeholder |

## Файлы

- [`.fdroid.yml`](.fdroid.yml) — черновик build metadata (заполнять `commit` при релизе)
- [`metadata/en-US/`](metadata/en-US/) — skeleton описания (Fastlane/Triple-T)
- Иконка: `metadata/en-US/images/icon.png` (генерируется `scripts/branding/generate-app-icons.ps1`)

## Локальная проверка (когда рецепт готов)

```bash
# Установить fdroidserver, клонировать репозиторий
pip install fdroidserver
fdroid lint packaging/android/fdroid/.fdroid.yml
fdroid build -v --on-server com.cubatarium
```

Для официального репозитория метаданные обычно живут в [fdroiddata](https://gitlab.com/fdroid/fdroiddata) как `metadata/com.cubatarium.yml`.

## Блокеры до реальной подачи

1. Закрепить git tags/commits для `glm`, `nlohmann/json`, `freetype` в Android CMake path
2. Аудит placeholder-лицензий у creatures (`Replace with CC-licensed…`)
3. Выбрать `assembleRelease` (APK) — F-Droid традиционно распространяет APK; F-Droid подписывает своим ключом
4. Прогнать полную сборку на чистой машине / `fdroid build`
5. Указать реальный `SourceCode` URL в `.fdroid.yml`

## Подпись

Upload keystore для Play **не нужен** для сборки F-Droid — они подписывают release своим ключом после сборки из исходников.
