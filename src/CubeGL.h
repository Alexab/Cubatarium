#ifndef CUBEGL_H
#define CUBEGL_H

//#include <QOpenGLBuffer>
//#include <QOpenGLTexture>
//#include <QVector3D>
//#include <QVector2D>
#include <vector>
#include <glm/glm.hpp>
// GLEW будет включен в .cpp файле после инициализации GLFW
// Forward declaration для OpenGL типов
typedef unsigned int GLuint;
typedef unsigned short GLushort;
#include "Cube.h"


namespace cutum {

struct VertexData
{
 glm::vec3 position;
 glm::vec2 texCoord;

 VertexData() = default;
 VertexData(const VertexData &) = default;
 VertexData& operator = (const VertexData &) = default;
 VertexData(glm::vec3 position, glm::vec2 texCoord);
};

class CubeGL: public Cube
{
public:
 CubeGL();
 CubeGL(const CubeGL &copy);
 CubeGL& operator = (const CubeGL &copy);

 ~CubeGL();

 void Init(const glm::mat4 &initial_pose, float size=1.0);
 void UpdateVertices();

 std::vector<VertexData> GetVertices() const;
 std::vector<VertexData> GetVerticesInitialPos() const;
 std::vector<GLushort> GetIndices() const;
 GLuint GetArrayBuf();
 GLuint GetIndexBuf();

private:
 std::vector<VertexData> VerticesInitialPos;
 std::vector<VertexData> Vertices;
 std::vector<GLushort> Indices;
 GLuint arrayBuf;
 GLuint indexBuf;
};

extern std::shared_ptr<Cube> NewCube();

}

#endif // CUBEGL_H
