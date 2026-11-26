#pragma once

#include "Render/GlIncludes.h"
#include <utility>

namespace cutum
{
class RenderOverlayQuad
{
public:
  RenderOverlayQuad() = default;
  RenderOverlayQuad(const RenderOverlayQuad &) = delete;
  RenderOverlayQuad &operator=(const RenderOverlayQuad &) = delete;
  ~RenderOverlayQuad() { Reset(); }

  bool Initialize()
  {
    if (Vao)
      return true;
    if (glGetError() != GL_NO_ERROR)
      return false;
    struct Binding
    {
      GLint vao{}, array{};
      Binding()
      {
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array);
      }
      ~Binding()
      {
        glBindVertexArray(static_cast<GLuint>(vao));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(array));
      }
    } binding;
    RenderOverlayQuad next;
    const float vertices[] = {
        -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f};
    const unsigned int indices[] = {0, 1, 2, 0, 2, 3};
    glGenVertexArrays(1, &next.Vao);
    glGenBuffers(1, &next.Vertices);
    glGenBuffers(1, &next.Indices);
    if (!next.Vao || !next.Vertices || !next.Indices)
      return false;
    glBindVertexArray(next.Vao);
    glBindBuffer(GL_ARRAY_BUFFER, next.Vertices);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, next.Indices);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<const void *>(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    if (glGetError() != GL_NO_ERROR)
      return false;
    std::swap(Vao, next.Vao);
    std::swap(Vertices, next.Vertices);
    std::swap(Indices, next.Indices);
    return true;
  }

  void Reset()
  {
    if (Vao)
      glDeleteVertexArrays(1, &Vao);
    if (Vertices)
      glDeleteBuffers(1, &Vertices);
    if (Indices)
      glDeleteBuffers(1, &Indices);
    Vao = Vertices = Indices = 0;
  }

  GLuint VertexArray() const { return Vao; }
  GLuint VertexBuffer() const { return Vertices; }
  GLuint IndexBuffer() const { return Indices; }

private:
  GLuint Vao{}, Vertices{}, Indices{};
};
} // namespace cutum
