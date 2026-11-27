#pragma once

#include "Render/GlIncludes.h"
#include <array>
#include <cstddef>

namespace cutum
{
template<std::size_t IndexedBindings>
class ComputeBufferBindings
{
public:
  ComputeBufferBindings()
  {
    glGetIntegerv(GL_CURRENT_PROGRAM, &Program);
    glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING, &Buffer);
    for (GLuint i = 0; i < IndexedBindings; ++i)
    {
      glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, i, &Indexed[i]);
      glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_START, i, &Start[i]);
      glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_SIZE, i, &Size[i]);
    }
  }

  ~ComputeBufferBindings()
  {
    glUseProgram(static_cast<GLuint>(Program));
    for (GLuint i = 0; i < IndexedBindings; ++i)
      if (Indexed[i] && Size[i] > 0)
        glBindBufferRange(GL_SHADER_STORAGE_BUFFER, i, static_cast<GLuint>(Indexed[i]), Start[i], Size[i]);
      else
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, i, static_cast<GLuint>(Indexed[i]));
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, static_cast<GLuint>(Buffer));
  }

  ComputeBufferBindings(const ComputeBufferBindings &) = delete;
  ComputeBufferBindings &operator=(const ComputeBufferBindings &) = delete;

private:
  GLint Program{}, Buffer{};
  std::array<GLint, IndexedBindings> Indexed{};
  std::array<GLint64, IndexedBindings> Start{}, Size{};
};
} // namespace cutum
