#ifndef CUBEGL_H
#define CUBEGL_H

// #include <QOpenGLBuffer>
// #include <QOpenGLTexture>
// #include <QVector3D>
// #include <QVector2D>
#include <glm/glm.hpp>
#include <vector>
// GLEW will be included in .cpp file after GLFW initialization
// Forward declaration for OpenGL types
typedef unsigned int GLuint;
typedef unsigned short GLushort;
#include "Cube.h"

namespace cutum
{

struct VertexData
{
  glm::vec3 position;
  glm::vec2 texCoord;

  VertexData() = default;
  VertexData(const VertexData &) = default;
  VertexData &operator=(const VertexData &) = default;
  VertexData(glm::vec3 position, glm::vec2 texCoord);
};

class UCubeGL : public UCube
{
public:
  UCubeGL();
  UCubeGL(const UCubeGL &copy);
  UCubeGL &operator=(const UCubeGL &copy);

  ~UCubeGL();

  void Init(const glm::mat4 &initial_pose, float size = 1.0);
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

extern std::shared_ptr<UCube> NewCube();

} // namespace cutum

#endif // CUBEGL_H
