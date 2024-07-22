QT += core gui widgets
QT += network

CONFIG += c++17

TARGET = Cubatarium
TEMPLATE = app

VERSION = "0.0.1.1"

RC_ICONS = resources/Cubatarium.ico

windows {
message("Cubatarium: using "msvc-$$(VisualStudioVersion) compiler)
DESTDIR = $$PWD/bin
} else {
DESTDIR = $$PWD/bin
}


SOURCES += src/main.cpp \
  src/Camera.cpp \
  src/Core.cpp \
  src/Cube.cpp \
  src/CubeGL.cpp \
  src/Object.cpp \
  src/ObjectImplementation.cpp \
  src/ObjectStorage.cpp \
  src/TextureBase.cpp \
  src/TextureCube.cpp \
  src/User.cpp \
  src/ViewEngine.cpp \
  src/World.cpp

SOURCES += \
    src/MainWidget.cpp \
    src/GeometryEngine.cpp

HEADERS += \
    src/Camera.h \
    src/Core.h \
    src/Cube.h \
    src/CubeGL.h \
    src/Object.h \
    src/ObjectImplementation.h \
    src/ObjectStorage.h \
    src/TextureBase.h \
    src/TextureCube.h \
    src/User.h \
    src/ViewEngine.h \
    src/World.h \
    src/MainWidget.h \
    src/GeometryEngine.h

RESOURCES += \
    resources/shaders.qrc

