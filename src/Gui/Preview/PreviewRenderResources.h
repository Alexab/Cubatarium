#pragma once

#include "Render/Pipeline/GlStateScope.h"
#include <cstddef>
#include <utility>

namespace cutum
{

inline void AllocatePreviewColor(int size, GLint format = GL_RGBA)
{
  GLint unpack = 0;
  glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack);
  if (unpack)
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  glTexImage2D(GL_TEXTURE_2D, 0, format, size, size, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  if (unpack)
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, GLuint(unpack));
}

struct PreviewRenderTarget
{
  PreviewRenderTarget() = default;
  PreviewRenderTarget(const PreviewRenderTarget &) = delete;
  PreviewRenderTarget &operator=(const PreviewRenderTarget &) = delete;
  ~PreviewRenderTarget() { Reset(); }

  bool Initialize(int size)
  {
    UGlStateScope state(static_cast<GlStateMask>(GlStateBit::ViewportFb));
    PreviewRenderTarget next;
    glGenFramebuffers(1, &next.Framebuffer);
    glGenTextures(1, &next.Color);
    glGenRenderbuffers(1, &next.Depth);
    if (!next.Framebuffer || !next.Color || !next.Depth)
      return false;
    glBindTexture(GL_TEXTURE_2D, next.Color);
    AllocatePreviewColor(size);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, next.Framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, next.Color, 0);
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
#endif
    GLint previous = 0;
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &previous);
    glBindRenderbuffer(GL_RENDERBUFFER, next.Depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);
    glBindRenderbuffer(GL_RENDERBUFFER, GLuint(previous));
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, next.Depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      return false;
    Swap(next);
    return true;
  }

  void Swap(PreviewRenderTarget &other) noexcept
  {
    std::swap(Framebuffer, other.Framebuffer);
    std::swap(Color, other.Color);
    std::swap(Depth, other.Depth);
  }

  void Reset() noexcept
  {
    if (Framebuffer)
      glDeleteFramebuffers(1, &Framebuffer);
    if (Color)
      glDeleteTextures(1, &Color);
    if (Depth)
      glDeleteRenderbuffers(1, &Depth);
    Framebuffer = Color = Depth = 0;
  }

  GLuint Framebuffer{0}, Color{0}, Depth{0};
};

struct PreviewCubeMesh
{
  static constexpr std::size_t kCubeVertexBytes = 24 * 5 * sizeof(float);
  static constexpr std::size_t kCubeIndexBytes = 36 * sizeof(unsigned int);

  PreviewCubeMesh() = default;
  PreviewCubeMesh(const PreviewCubeMesh &) = delete;
  PreviewCubeMesh &operator=(const PreviewCubeMesh &) = delete;
  ~PreviewCubeMesh() { Reset(); }

  bool Initialize()
  {
    const float cubeShift = 1.0f / 6.0f;
    const float vertices[] = {
        -0.5f, -0.5f, 0.5f,  0.0f, 1.0f,  0.5f,  -0.5f, 0.5f,  cubeShift, 1.0f,
        -0.5f, 0.5f,  0.5f,  0.0f, 0.0f,  0.5f,  0.5f,  0.5f,  cubeShift, 0.0f,
        0.5f,  -0.5f, 0.5f,  cubeShift, 1.0f, 0.5f,  -0.5f, -0.5f, cubeShift * 2.0f,
        1.0f,  0.5f,  0.5f,  0.5f,  cubeShift, 0.0f, 0.5f,  0.5f,  -0.5f, cubeShift * 2.0f,
        0.0f,  0.5f,  -0.5f, -0.5f, cubeShift * 2.0f, 1.0f, -0.5f, -0.5f, -0.5f,
        cubeShift * 3.0f, 1.0f, 0.5f, 0.5f, -0.5f, cubeShift * 2.0f, 0.0f, -0.5f, 0.5f,
        -0.5f, cubeShift * 3.0f, 0.0f, -0.5f, -0.5f, -0.5f, cubeShift * 3.0f, 1.0f,
        -0.5f, -0.5f, 0.5f,  cubeShift * 4.0f, 1.0f, -0.5f, 0.5f, -0.5f, cubeShift * 3.0f,
        0.0f,  -0.5f, 0.5f,  0.5f,  cubeShift * 4.0f, 0.0f, -0.5f, 0.5f,  0.5f,
        cubeShift * 4.0f, 0.0f, 0.5f, 0.5f, 0.5f, cubeShift * 5.0f, 0.0f, -0.5f, 0.5f,
        -0.5f, cubeShift * 4.0f, 1.0f, 0.5f, 0.5f, -0.5f, cubeShift * 5.0f, 1.0f,
        -0.5f, -0.5f, -0.5f, cubeShift * 5.0f, 0.0f, 0.5f, -0.5f, -0.5f, 1.0f, 0.0f,
        -0.5f, -0.5f, 0.5f,  cubeShift * 5.0f, 1.0f, 0.5f, -0.5f, 0.5f, 1.0f, 1.0f,
    };
    const unsigned int indices[] = {
        0,  1,  2,  2,  1,  3,  4,  5,  6,  6,  5,  7,  8,  9,  10, 10, 9,  11,
        12, 13, 14, 14, 13, 15, 16, 17, 18, 18, 17, 19, 20, 21, 22, 22, 21, 23,
    };
    return Initialize(vertices, indices);
  }

  bool Initialize(const float (&vertices)[24 * 5], const unsigned int (&indices)[36])
  {
    if (Vao)
      return true;

    PreviewCubeMesh next;
    glGenVertexArrays(1, &next.Vao);
    glGenBuffers(1, &next.Vbo);
    glGenBuffers(1, &next.Ebo);
    if (!next.Vao || !next.Vbo || !next.Ebo)
      return false;

    GLint callerVao = 0, callerArray = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &callerVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &callerArray);
    glBindVertexArray(next.Vao);
    glBindBuffer(GL_ARRAY_BUFFER, next.Vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, next.Ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    GLint vertexBytes = 0, indexBytes = 0;
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &vertexBytes);
    glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &indexBytes);
    const bool complete = vertexBytes == GLint(sizeof(vertices)) && indexBytes == GLint(sizeof(indices));
    if (complete)
    {
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                            reinterpret_cast<void *>(3 * sizeof(float)));
      glEnableVertexAttribArray(1);
      Swap(next);
    }
    glBindVertexArray(GLuint(callerVao));
    glBindBuffer(GL_ARRAY_BUFFER, GLuint(callerArray));
    return complete;
  }

  void Swap(PreviewCubeMesh &other) noexcept
  {
    std::swap(Vao, other.Vao);
    std::swap(Vbo, other.Vbo);
    std::swap(Ebo, other.Ebo);
  }

  void Reset() noexcept
  {
    if (Vao)
      glDeleteVertexArrays(1, &Vao);
    if (Vbo)
      glDeleteBuffers(1, &Vbo);
    if (Ebo)
      glDeleteBuffers(1, &Ebo);
    Vao = Vbo = Ebo = 0;
  }

  GLuint Vao{0}, Vbo{0}, Ebo{0};
};

} // namespace cutum
