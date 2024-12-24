cmake_minimum_required(VERSION 3.10)

project(Cubatarium VERSION 0.0.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Network)

set(SOURCES
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
    src/MainWidget.cpp
    src/GeometryEngine.cpp
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
    src/MainWidget.h
    src/GeometryEngine.h
)

set(RESOURCES
    resources/shaders.qrc
)

if(WIN32)
    set(ICON_RESOURCE resources/Cubatarium.ico)
endif()

set(CMAKE_BINARY_DIR ${CMAKE_SOURCE_DIR}/bin)

qt5_add_resources(RESOURCE_FILES ${RESOURCES})

qt5_wrap_cpp(MOC_SOURCES src/MainWidget.h)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${RESOURCE_FILES} ${MOC_SOURCES} ${ICON_RESOURCE})

target_link_libraries(${PROJECT_NAME} Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network)

if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE "WIN32")
    set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

