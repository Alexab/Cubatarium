cmake_minimum_required(VERSION 3.10)

project(Cubatarium VERSION 0.0.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# Настройки компилятора для отладки
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(MSVC)
        add_compile_options(/Zi /Od)
    else()
        add_compile_options(-g -O0)
    endif()
endif()

# Удаляем Qt зависимости
# find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Network)

# Добавляем GLFW, GLM и GLEW
find_package(PkgConfig REQUIRED)
find_package(OpenGL REQUIRED)

# GLFW
if(WIN32)
    # Для Windows можно использовать vcpkg или скачать GLFW
    find_package(glfw3 REQUIRED)
else()
    find_package(glfw3 REQUIRED)
endif()

# GLM
find_package(glm REQUIRED)

# GLEW
find_package(GLEW REQUIRED)

find_package(Freetype REQUIRED)

# nlohmann/json
find_package(nlohmann_json REQUIRED)

# stb_image для загрузки текстур
set(STB_IMAGE_HEADER "${CMAKE_SOURCE_DIR}/src/stb_image.h")
if(NOT EXISTS ${STB_IMAGE_HEADER})
    file(DOWNLOAD 
        "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h"
        ${STB_IMAGE_HEADER}
    )
endif()

set(SOURCES
    src/stb_image.cpp
    src/main.cpp
    src/Camera.cpp
    src/Core.cpp
    src/Cube.cpp
    src/CubeGL.cpp
    src/Object.cpp
    src/ObjectImplementation.cpp
    src/ObjectStorage.cpp
    src/TextureBase.cpp
    src/TextureCube.cpp
    src/User.cpp
    src/ViewEngine.cpp
    src/World.cpp
    src/WindowManager.cpp
    src/GeometryEngine.cpp
    src/Octree.cpp
    src/InputManager.cpp
    src/ShaderManager.cpp
    src/TextRenderer.cpp
)

set(HEADERS
    src/Camera.h
    src/Core.h
    src/Cube.h
    src/CubeGL.h
    src/Object.h
    src/ObjectImplementation.h
    src/ObjectStorage.h
    src/TextureBase.h
    src/TextureCube.h
    src/User.h
    src/ViewEngine.h
    src/World.h
    src/WindowManager.h
    src/GeometryEngine.h
    src/Octree.h
    src/InputManager.h
    src/ShaderManager.h
    src/TextRenderer.h
    src/stb_image.h
)

# Удаляем Qt ресурсы
# set(RESOURCES
#     resources/shaders.qrc
# )

if(WIN32)
    set(ICON_RESOURCE resources/Cubatarium.ico)
endif()

set(CMAKE_BINARY_DIR ${CMAKE_SOURCE_DIR}/bin)

# Удаляем Qt специфичные команды
# qt5_add_resources(RESOURCE_FILES ${RESOURCES})
# qt5_wrap_cpp(MOC_SOURCES src/MainWidget.h)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${ICON_RESOURCE})

# Заменяем Qt библиотеки на GLFW, OpenGL и GLEW
target_link_libraries(${PROJECT_NAME} 
    OpenGL::GL
    glfw
    glm::glm
    GLEW::GLEW
    Freetype::Freetype
    nlohmann_json::nlohmann_json
)

if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE "WIN32")
    # Убираем WIN32_EXECUTABLE для консольного приложения
    # set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}
)

