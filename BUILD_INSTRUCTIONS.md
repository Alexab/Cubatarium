# Инструкции по сборке Cubatarium без Qt зависимостей

## Обзор изменений

Проект был успешно переведен с Qt на стандартные C++ библиотеки и OpenGL:

### Замененные компоненты:
- **QVector3D/QVector2D** → **glm::vec3/glm::vec2**
- **QMatrix4x4** → **glm::mat4**
- **QJsonDocument/QJsonObject** → **nlohmann/json**
- **QOpenGLBuffer** → **стандартные OpenGL буферы**
- **QOpenGLTexture** → **GLuint**
- **QFile** → **std::ifstream/std::ofstream**

### Удаленные файлы:
- `src/MainWidget.cpp` и `src/MainWidget.h` (использовали Qt Widgets)
- Qt ресурсные файлы

## Зависимости

Для сборки проекта требуются следующие библиотеки:

1. **GLM** - математическая библиотека
2. **GLFW** - управление окнами и контекстом OpenGL
3. **GLEW** - загрузка OpenGL расширений
4. **nlohmann/json** - работа с JSON
5. **OpenGL** - графическая библиотека

## Установка зависимостей

### Windows (vcpkg):
```bash
vcpkg install glm glfw3 glew nlohmann-json
```

### Ubuntu/Debian:
```bash
sudo apt-get install libglm-dev libglfw3-dev libglew-dev nlohmann-json3-dev
```

### macOS (Homebrew):
```bash
brew install glm glfw glew nlohmann-json
```

## Сборка проекта

### Использование CMake:
```bash
mkdir build
cd build
cmake ..
make
```

### Альтернативная сборка (если CMake недоступен):
Можно использовать компилятор напрямую:

```bash
# Windows (MSVC)
cl /std:c++17 /I<path_to_glm> /I<path_to_glfw> /I<path_to_glew> /I<path_to_json> src/*.cpp -o Cubatarium.exe

# Linux/macOS (GCC/Clang)
g++ -std=c++17 -I<path_to_glm> -I<path_to_glfw> -I<path_to_glew> -I<path_to_json> src/*.cpp -o Cubatarium -lglfw -lGLEW -lGL
```

## Тестирование

Для проверки базовой функциональности создан тестовый файл `test_build.cpp`:

```bash
# Сборка теста
g++ -std=c++17 test_build.cpp -o test_build -lglm

# Запуск теста
./test_build
```

## Структура проекта

```
Cubatarium/
├── src/                    # Исходный код
│   ├── main.cpp           # Главная функция
│   ├── Core.cpp           # Основная логика (исправлен)
│   ├── CubeGL.cpp         # OpenGL кубы (исправлен)
│   ├── GeometryEngine.cpp # Рендеринг (исправлен)
│   └── ...
├── CMakeLists.txt         # Конфигурация CMake (обновлен)
├── test_build.cpp         # Тестовый файл
└── BUILD_INSTRUCTIONS.md  # Этот файл
```

## Известные проблемы

1. **CMake не найден**: Установите CMake или используйте прямую компиляцию
2. **GLM не найден**: Установите библиотеку GLM
3. **OpenGL ошибки**: Убедитесь, что драйверы OpenGL установлены

## Следующие шаги

1. Настроить систему сборки для вашей платформы
2. Добавить обработку событий (вместо Qt событий)
3. Реализовать пользовательский интерфейс (если необходимо)
4. Добавить поддержку текстур и шейдеров

## Контакты

При возникновении проблем с сборкой, проверьте:
- Установлены ли все зависимости
- Правильность путей к библиотекам
- Версию компилятора (требуется C++17)
