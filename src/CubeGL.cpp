#include "CubeGL.h"
#include <GL/glew.h>

namespace cutum
{

VertexData::VertexData(glm::vec3 position, glm::vec2 texCoord)
    : position(position), texCoord(texCoord)
{
}

UCubeGL::UCubeGL() : UCube()
{
  glGenBuffers(1, &arrayBuf);
  glGenBuffers(1, &indexBuf);
}

UCubeGL::UCubeGL(const UCubeGL &copy) : UCube(copy)
{
  glGenBuffers(1, &arrayBuf);
  glGenBuffers(1, &indexBuf);

  Vertices = copy.Vertices;
  Indices = copy.Indices;

  glBindBuffer(GL_ARRAY_BUFFER, arrayBuf);
  glBufferData(GL_ARRAY_BUFFER, Vertices.size() * sizeof(VertexData),
               Vertices.data(), GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuf);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, Indices.size() * sizeof(GLushort),
               Indices.data(), GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

UCubeGL &UCubeGL::operator=(const UCubeGL &copy)
{
  UCube::operator=(copy);
  Vertices = copy.Vertices;
  Indices = copy.Indices;

  glBindBuffer(GL_ARRAY_BUFFER, arrayBuf);
  glBufferData(GL_ARRAY_BUFFER, Vertices.size() * sizeof(VertexData),
               Vertices.data(), GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuf);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, Indices.size() * sizeof(GLushort),
               Indices.data(), GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  return *this;
}

UCubeGL::~UCubeGL()
{
  glDeleteBuffers(1, &arrayBuf);
  glDeleteBuffers(1, &indexBuf);
}

void UCubeGL::Init(const glm::mat4 &initial_pose, float size)
{
  UCube::Init(initial_pose, size);
  float size2 = Size * 0.5f;

  float cube_shift = 1.0f / 6.0f;

  // Vertex data for face 0 (CUBE_SIDE_NEAR) — V flipped for side faces
  VerticesInitialPos.clear();
  VerticesInitialPos.emplace_back(glm::vec3(-size2, -size2, size2),
                                  glm::vec2(0.0f, 1.0f)); // v0
  VerticesInitialPos.emplace_back(glm::vec3(size2, -size2, size2),
                                  glm::vec2(cube_shift * 1.0f, 1.0f)); // v1
  VerticesInitialPos.emplace_back(glm::vec3(-size2, size2, size2),
                                  glm::vec2(0.0f, 0.0f)); // v2
  VerticesInitialPos.emplace_back(glm::vec3(size2, size2, size2),
                                  glm::vec2(cube_shift * 1.0f, 0.0f)); // v3

  // Vertex data for face 1 (CUBE_SIDE_RIGHT)
  VerticesInitialPos.emplace_back(glm::vec3(size2, -size2, size2),
                                  glm::vec2(cube_shift * 1.0f, 1.0f)); // v4
  VerticesInitialPos.emplace_back(glm::vec3(size2, -size2, -size2),
                                  glm::vec2(cube_shift * 2.0f, 1.0f)); // v5
  VerticesInitialPos.emplace_back(glm::vec3(size2, size2, size2),
                                  glm::vec2(cube_shift * 1.0f, 0.0f)); // v6
  VerticesInitialPos.emplace_back(glm::vec3(size2, size2, -size2),
                                  glm::vec2(cube_shift * 2.0f, 0.0f)); // v7

  // Vertex data for face 2 (CUBE_SIDE_FAR)
  VerticesInitialPos.emplace_back(glm::vec3(size2, -size2, -size2),
                                  glm::vec2(cube_shift * 2.0f, 1.0f)); // v8
  VerticesInitialPos.emplace_back(glm::vec3(-size2, -size2, -size2),
                                  glm::vec2(cube_shift * 3.0f, 1.0f)); // v9
  VerticesInitialPos.emplace_back(glm::vec3(size2, size2, -size2),
                                  glm::vec2(cube_shift * 2.0f, 0.0f)); // v10
  VerticesInitialPos.emplace_back(glm::vec3(-size2, size2, -size2),
                                  glm::vec2(cube_shift * 3.0f, 0.0f)); // v11

  // Vertex data for face 3 (CUBE_SIDE_LEFT)
  VerticesInitialPos.emplace_back(glm::vec3(-size2, -size2, -size2),
                                  glm::vec2(cube_shift * 3.0f, 1.0f)); // v12
  VerticesInitialPos.emplace_back(glm::vec3(-size2, -size2, size2),
                                  glm::vec2(cube_shift * 4.0f, 1.0f)); // v13
  VerticesInitialPos.emplace_back(glm::vec3(-size2, size2, -size2),
                                  glm::vec2(cube_shift * 3.0f, 0.0f)); // v14
  VerticesInitialPos.emplace_back(glm::vec3(-size2, size2, size2),
                                  glm::vec2(cube_shift * 4.0f, 0.0f)); // v15

  // Vertex data for face 4 (CUBE_SIDE_TOP)
  VerticesInitialPos.emplace_back(glm::vec3(-size2, size2, size2),
                                  glm::vec2(cube_shift * 4.0f, 0.0f)); // v16
  VerticesInitialPos.emplace_back(glm::vec3(size2, size2, size2),
                                  glm::vec2(cube_shift * 5.0f, 0.0f)); // v17
  VerticesInitialPos.emplace_back(glm::vec3(-size2, size2, -size2),
                                  glm::vec2(cube_shift * 4.0f, 1.0f)); // v18
  VerticesInitialPos.emplace_back(glm::vec3(size2, size2, -size2),
                                  glm::vec2(cube_shift * 5.0f, 1.0f)); // v19

  // Vertex data for face 5 (CUBE_SIDE_BOTTOM)
  VerticesInitialPos.emplace_back(glm::vec3(-size2, -size2, -size2),
                                  glm::vec2(cube_shift * 5.0f, 0.0f)); // v16
  VerticesInitialPos.emplace_back(glm::vec3(size2, -size2, -size2),
                                  glm::vec2(1.0, 0.0f)); // v17
  VerticesInitialPos.emplace_back(glm::vec3(-size2, -size2, size2),
                                  glm::vec2(cube_shift * 5.0f, 1.0f)); // v18
  VerticesInitialPos.emplace_back(glm::vec3(size2, -size2, size2),
                                  glm::vec2(1.0, 1.0f)); // v19

  for (auto &vert : VerticesInitialPos)
  {
    vert.position = initial_pose * glm::vec4(vert.position, 1.0f);
  }

  Indices = {
      0,  1,  2,  3,  3,      // Face 0 - triangle strip ( v0,  v1,  v2,  v3)
      4,  4,  5,  6,  7,  7,  // Face 1 - triangle strip ( v4,  v5,  v6,  v7)
      8,  8,  9,  10, 11, 11, // Face 2 - triangle strip ( v8,  v9, v10, v11)
      12, 12, 13, 14, 15, 15, // Face 3 - triangle strip (v12, v13, v14, v15)
      16, 16, 17, 18, 19, 19, // Face 4 - triangle strip (v16, v17, v18, v19)
      20, 20, 21, 22, 23      // Face 5 - triangle strip (v20, v21, v22, v23)
  };

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuf);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, Indices.size() * sizeof(GLushort),
               Indices.data(), GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

  UpdateVertices();
}

void UCubeGL::UpdateVertices()
{
  Vertices.resize(VerticesInitialPos.size());
  for (size_t i = 0; i < Vertices.size(); i++)
  {
    Vertices[i].position =
        ObjectPose * glm::vec4(VerticesInitialPos[i].position, 1.0f);
    Vertices[i].texCoord = VerticesInitialPos[i].texCoord;
  }

  glBindBuffer(GL_ARRAY_BUFFER, arrayBuf);
  glBufferData(GL_ARRAY_BUFFER, Vertices.size() * sizeof(VertexData),
               Vertices.data(), GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

std::vector<VertexData> UCubeGL::GetVertices() const { return Vertices; }

std::vector<VertexData> UCubeGL::GetVerticesInitialPos() const
{
  return VerticesInitialPos;
}

std::vector<GLushort> UCubeGL::GetIndices() const { return Indices; }

GLuint UCubeGL::GetArrayBuf() { return arrayBuf; }

GLuint UCubeGL::GetIndexBuf() { return indexBuf; }

std::shared_ptr<UCube> NewCube() { return std::make_shared<UCubeGL>(); }

} // namespace cutum
