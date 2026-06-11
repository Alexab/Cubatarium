# Установка зависимостей для рефакторинга

## Обзор

После рефакторинга проект использует следующие библиотеки вместо Qt:

- **GLFW** - создание окон и контекста OpenGL
- **GLEW** - загрузчик OpenGL расширений  
- **GLM** - математическая библиотека
- **stb_image** - загрузка изображений

## Windows

### Вариант 1: vcpkg (рекомендуется)

На Windows проект линкуется **статически** (triplet `x64-windows-static`, CRT `/MT` для Release).
VC++ Redistributable и DLL от vcpkg рядом с exe не нужны.

```bash
# Установка vcpkg (если не установлен)
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.bat

# Установка зависимостей (static triplet)
vcpkg install glfw3 glew glm freetype nlohmann-json --triplet x64-windows-static

# Интеграция с CMake
vcpkg integrate install
```

После смены triplet удалите `bin/CMakeCache.txt` (или задачу clean-cache) и перезапустите configure.

### Вариант 2: Ручная установка

1. **GLFW**: Скачать с https://www.glfw.org/download.html
2. **GLM**: Скачать с https://github.com/g-truc/glm/releases
3. **GLEW**: Скачать с http://glew.sourceforge.net/

### CMake настройки для Windows

```cmake
# В CMakeLists.txt добавить:
set(CMAKE_TOOLCHAIN_FILE "path/to/vcpkg/scripts/buildsystems/vcpkg.cmake")

# Или указать пути к библиотекам:
set(GLFW_DIR "path/to/glfw")
set(GLM_DIR "path/to/glm") 
set(GLEW_DIR "path/to/glew")
```

## Linux (Ubuntu/Debian)

```bash
# Установка зависимостей
sudo apt-get update
sudo apt-get install build-essential cmake
sudo apt-get install libglfw3-dev
sudo apt-get install libglm-dev
sudo apt-get install libglew-dev
sudo apt-get install libgl1-mesa-dev
```

## Linux (Fedora/RHEL)

```bash
# Установка зависимостей
sudo dnf install gcc-c++ cmake
sudo dnf install glfw-devel
sudo dnf install glm-devel
sudo dnf install glew-devel
sudo dnf install mesa-libGL-devel
```

## macOS

### Вариант 1: Homebrew (рекомендуется)

```bash
# Установка Homebrew (если не установлен)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Установка зависимостей
brew install glfw
brew install glm
brew install glew
```

### Вариант 2: MacPorts

```bash
sudo port install glfw3
sudo port install glm
sudo port install glew
```

## Проверка установки

### Тест GLFW

```cpp
#include <GLFW/glfw3.h>

int main() {
    if (!glfwInit()) {
        return -1;
    }
    glfwTerminate();
    return 0;
}
```

### Тест GLM

```cpp
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

int main() {
    glm::mat4 matrix = glm::mat4(1.0f);
    glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f);
    return 0;
}
```

### Тест GLEW

```cpp
#include <GL/glew.h>
#include <GLFW/glfw3.h>

int main() {
    if (!glfwInit()) return -1;
    
    GLFWwindow* window = glfwCreateWindow(640, 480, "Test", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return -1;
    }
    
    glfwMakeContextCurrent(window);
    
    if (glewInit() != GLEW_OK) {
        return -1;
    }
    
    glfwTerminate();
    return 0;
}
```

## Сборка проекта

```bash
# Создание директории сборки
mkdir build
cd build

# Конфигурация CMake
cmake ..

# Сборка
make -j$(nproc)  # Linux/macOS
# или
cmake --build . --config Release  # Windows
```

## Устранение проблем

### Ошибка "GLFW not found"

```bash
# Установить pkg-config
sudo apt-get install pkg-config  # Ubuntu/Debian
sudo dnf install pkgconfig       # Fedora

# Проверить установку GLFW
pkg-config --modversion glfw3
```

### Ошибка "OpenGL not found"

```bash
# Установить OpenGL development headers
sudo apt-get install libgl1-mesa-dev  # Ubuntu/Debian
sudo dnf install mesa-libGL-devel     # Fedora
```

### Ошибка "GLEW not found"

```bash
# Проверить установку GLEW
pkg-config --modversion glew
```

### Проблемы с CMake на Windows

```cmake
# Добавить в CMakeLists.txt для отладки:
find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
    message(FATAL_ERROR "GLFW not found. Please install it.")
endif()

find_package(glm QUIET)
if(NOT glm_FOUND)
    message(FATAL_ERROR "GLM not found. Please install it.")
endif()

find_package(GLEW QUIET)
if(NOT GLEW_FOUND)
    message(FATAL_ERROR "GLEW not found. Please install it.")
endif()
```

## Альтернативные варианты установки

### Conan Package Manager

```bash
# Установка Conan
pip install conan

# Создание conanfile.txt
[requires]
glfw/3.3.8
glm/0.9.9.8
glew/2.2.0

[generators]
cmake_find_package
```

### Spack Package Manager

```bash
# Установка Spack
git clone https://github.com/spack/spack.git
source spack/share/spack/setup-env.sh

# Установка пакетов
spack install glfw
spack install glm
spack install glew
```
