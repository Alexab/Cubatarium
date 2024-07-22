#ifndef GEOMETRYENGINE_H
#define GEOMETRYENGINE_H

#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLTexture>
#include "Object.h"
#include "TextureBase.h"
#include "TextureCube.h"
#include "World.h"
#include "CubeGL.h"
#include <optional>

namespace cutum {

class Core;

class GeometryEngine : protected QOpenGLFunctions
{
public:
 GeometryEngine(std::shared_ptr<ObjectStorage> object_storage, std::shared_ptr<World> world, std::shared_ptr<TextureBaseStorage> texture_base_storage, std::shared_ptr<TextureCubeStorage> texture_cube_storage);
 virtual ~GeometryEngine();

 bool InitEngine();
 bool InitShaders();

 void SetPainter(std::shared_ptr<QPainter> painter);

 void Paint(int width_size, int height_size);

private:
 void DrawCubeGeometry();
 void DrawCube(std::shared_ptr<Cube> icube, std::shared_ptr<QOpenGLTexture> & texture);
 void DrawObject(std::shared_ptr<Object> object, size_t object_index, const std::map<size_t, TextureCube>& textures, bool is_intersection_exists, size_t intersecion_object_index, size_t intersecion_cube_index);
 void DrawCubeGeometry(const std::vector<std::shared_ptr<Object>>& objects, const QMatrix4x4& mvp_matrix, bool is_intersection_exists, size_t intersecion_object_index, size_t intersecion_cube_index);

private:
 //OpenGL uniform locations and values
 GLint alphaUniformLocation;
 GLfloat alpha=0.5;

 QOpenGLShaderProgram program;

 std::shared_ptr<QPainter> Painter;

 std::shared_ptr<TextureBaseStorage> TextureBaseStorageInstance;
 std::shared_ptr<TextureCubeStorage> TextureCubeStorageInstance;
 std::shared_ptr<World> WorldInstance;
 std::shared_ptr<ObjectStorage> ObjectStorageInstance;
};

}

#endif // GEOMETRYENGINE_H
