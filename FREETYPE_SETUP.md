# Настройка FreeType для Cubatarium

## Что такое FreeType?

FreeType - это библиотека для рендеринга шрифтов. Она позволяет отображать текст с использованием настоящих шрифтов вместо простых битовых карт.

## Установка FreeType

### Windows (vcpkg)

Если вы используете vcpkg, установите FreeType:

```bash
vcpkg install freetype
```

### Windows (ручная установка)

1. Скачайте FreeType с официального сайта: https://www.freetype.org/download.html
2. Распакуйте архив
3. Добавьте путь к библиотеке в переменные среды

### Linux

```bash
# Ubuntu/Debian
sudo apt-get install libfreetype6-dev

# CentOS/RHEL
sudo yum install freetype-devel

# Arch Linux
sudo pacman -S freetype2
```

### macOS

```bash
brew install freetype
```

## Проверка установки

После установки FreeType проект должен собираться без ошибок. При запуске вы увидите:

```
Loaded 128 characters from font: fonts/Roboto-Regular.ttf
```

## Использование

Теперь `TextRenderer` автоматически использует FreeType для рендеринга текста. Вы можете:

1. Заменить bundled-шрифт в `fonts/` и обновить `kBundledUiFontRelPath` в `src/App/Platform/GameAssets.h`
2. Изменить размер шрифта при вызове `Initialize(fontSize)`
3. Передать явный путь: `Initialize("fonts/Roboto-Regular.ttf", 24)`

## Поддерживаемые форматы шрифтов

FreeType поддерживает множество форматов:
- TrueType (.ttf)
- OpenType (.otf)
- Type 1 (.pfa, .pfb)
- CID-keyed Type 1
- CFF
- и другие

## Примеры использования

```cpp
// Инициализация с пользовательским шрифтом
textRenderer->Initialize("path/to/font.ttf", 24);

// Отображение текста
textRenderer->RenderText("Hello World!", 100, 100, 1.0f, glm::vec3(1.0f, 1.0f, 1.0f));
```
