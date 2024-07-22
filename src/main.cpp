/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtCore module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:BSD$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** BSD License Usage
** Alternatively, you may use this file under the terms of the BSD license
** as follows:
**
** "Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in
**     the documentation and/or other materials provided with the
**     distribution.
**   * Neither the name of The Qt Company Ltd nor the names of its
**     contributors may be used to endorse or promote products derived
**     from this software without specific prior written permission.
**
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include <QApplication>
#include <QLabel>
#include <QSurfaceFormat>

#ifndef QT_NO_OPENGL
#include "mainwidget.h"
#endif

#include "TextureBase.h"
#include "TextureCube.h"
#include "Object.h"
#include "World.h"
#include "Core.h"
#include "GeometryEngine.h"
#include "ViewEngine.h"
#include "ObjectStorage.h"


int main(int argc, char *argv[])
{
 using namespace cutum;

 QApplication app(argc, argv);

 QCursor cursor(Qt::BlankCursor);
 QApplication::setOverrideCursor(cursor);
 QApplication::changeOverrideCursor(cursor);

 QSurfaceFormat format;
 format.setDepthBufferSize(48);
 format.setSamples(4);
// format.setProfile(QSurfaceFormat::OpenGLContextProfile::CompatibilityProfile);
 QSurfaceFormat::setDefaultFormat(format);

 app.setApplicationName("Cubatarium");
 //app.setApplicationVersion(VER);

#ifndef QT_NO_OPENGL
 MainWidget widget;
 widget.setFormat(format);

 auto texture_base_instance = std::make_shared<TextureBaseStorage>();
 auto texture_cube_instance = std::make_shared<TextureCubeStorage>(texture_base_instance);

 auto object_storage = std::make_shared<ObjectStorage>(texture_cube_instance);
 auto view_engine = std::make_shared<ViewEngine>();

 auto world = std::make_shared<World>(object_storage, view_engine);

 auto geometry_engine = std::make_shared<GeometryEngine>(object_storage, world, texture_base_instance, texture_cube_instance);
 auto core = std::make_shared<Core>(texture_base_instance, texture_cube_instance,
                                    object_storage, world, geometry_engine, view_engine);

 widget.Init(core, world, geometry_engine, view_engine);
 widget.showMaximized();
 core->LoadSystem("config.json");
 widget.Run();
#else
 QLabel note("OpenGL Support required");
 note.show();
#endif
 bool result = app.exec();
 core->SaveSystem("config.json");
 return result;
}
