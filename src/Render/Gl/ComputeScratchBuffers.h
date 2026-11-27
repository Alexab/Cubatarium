#pragma once

#include "Render/Engine/TerrainGpuBuffers.h"
#include "Render/Gl/ComputeProgram.h"
#include <algorithm>
#include <array>
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
#include <EGL/egl.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <GLFW/glfw3.h>
#endif

namespace cutum
{
inline const void *CurrentComputeContext()
{
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  return eglGetCurrentContext();
#elif defined(_WIN32)
  return wglGetCurrentContext();
#else
  return glfwGetCurrentContext();
#endif
}

inline bool ComputeStorageAvailable()
{
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  return CurrentComputeContext() != nullptr;
#else
  return CurrentComputeContext() && glDispatchCompute && glGetInteger64i_v && glBindBufferBase &&
         (GLEW_VERSION_4_3 || (GLEW_ARB_compute_shader && GLEW_ARB_shader_storage_buffer_object));
#endif
}

// The render thread keeps one slot owned by its current context. Its platform
// window releases it before context teardown, including failed initialization.
template<std::size_t Count>
struct ComputeScratchBuffers
{
  std::array<GLuint, Count> names{};
  GLuint program{};

  template<typename Compile>
  bool Initialize(Compile compile)
  {
    const auto *current = CurrentComputeContext();
    if (!current || (Owner && current != Owner))
      return false;
    Owner = current;
    if (!CompileAttempted)
    {
      UComputeProgram candidate;
      if (compile(candidate))
        program = candidate.Release();
      CompileAttempted = true;
    }
    if (!program)
      return false;
    if (names.front())
      return true;
    glGenBuffers(static_cast<GLsizei>(Count), names.data());
    if (std::all_of(names.begin(), names.end(), [](GLuint name) { return name != 0; }))
      return true;
    DestroyBuffers();
    return false;
  }

  void Inspect(TerrainGpuBufferCensus &out, TerrainGpuBufferRole role) const
  {
    if (Owner && CurrentComputeContext() == Owner)
      for (GLuint name : names)
        out.Add(role, name);
  }

  void DestroyBuffers()
  {
    glDeleteBuffers(static_cast<GLsizei>(Count), names.data());
    names.fill(0);
  }

  // A platform may have already lost the native context. Its object names
  // cannot be deleted through a different current context.
  void Release(const void *context)
  {
    if (!context || context != Owner)
      return;
    if (CurrentComputeContext() == Owner)
    {
      DestroyBuffers();
      if (program)
        glDeleteProgram(program);
    }
    *this = {};
  }

private:
  const void *Owner{};
  bool CompileAttempted{};
};
} // namespace cutum
