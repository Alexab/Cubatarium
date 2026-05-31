# Шрифты для Cubatarium

Эта папка содержит шрифты, используемые в проекте Cubatarium.

## Текущие шрифты

- `arial.ttf` - Arial шрифт (скопирован из системной папки Windows)
- `calibri.ttf` - Calibri шрифт (скопирован из системной папки Windows)

## Добавление новых шрифтов

### Рекомендуемые открытые шрифты

Для кроссплатформенности рекомендуется использовать открытые шрифты:

1. **Roboto** - шрифт от Google
   - Скачать: https://github.com/google/fonts/tree/main/apache/roboto
   - Файл: `Roboto-Regular.ttf`

2. **DejaVu Sans** - популярный открытый шрифт
   - Скачать: https://dejavu-fonts.github.io/
   - Файл: `DejaVuSans.ttf`

3. **Liberation Sans** - шрифт от Red Hat
   - Скачать: https://github.com/liberationfonts/liberation-fonts
   - Файл: `LiberationSans-Regular.ttf`

### Инструкции по добавлению

1. Скачайте шрифт в формате TTF
2. Поместите файл в папку `fonts/`
3. Обновите код в `src/main.cpp`:
   - Добавьте путь к новому шрифту в массив `localFonts` в функции `GetFontPath()`
   - Добавьте путь в массив `alternativeFonts` в функции `main()`

### Пример обновления кода

```cpp
std::vector<std::string> localFonts = {
    "fonts/arial.ttf",
    "fonts/calibri.ttf",
    "fonts/Roboto-Regular.ttf"  // Новый шрифт
};
```

## Примечания

- Все шрифты должны быть в формате TTF
- Имена файлов должны быть в нижнем регистре
- Код автоматически проверит существование файла перед загрузкой
- Система имеет fallback на системные шрифты, если локальные недоступны


