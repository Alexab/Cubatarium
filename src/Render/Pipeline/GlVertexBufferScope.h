#pragma once

#include "Render/GlIncludes.h"
#include <initializer_list>

namespace cutum
{
class GlVertexBufferScope
{
public:
  GlVertexBufferScope()
  {
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &Vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &Array);
  }
  ~GlVertexBufferScope()
  {
    glBindVertexArray(static_cast<GLuint>(Vao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(Array));
  }
  GlVertexBufferScope(const GlVertexBufferScope &) = delete;
  GlVertexBufferScope &operator=(const GlVertexBufferScope &) = delete;

private:
  GLint Vao{}, Array{};
};

inline void RetireVertexBuffers(GLuint &vao, std::initializer_list<GLuint *> buffers)
{
  if (vao)
    glDeleteVertexArrays(1, &vao);
  vao = 0;
  for (auto *buffer : buffers)
  {
    if (*buffer)
      glDeleteBuffers(1, buffer);
    *buffer = 0;
  }
}
} // namespace cutum
