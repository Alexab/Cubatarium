#pragma once

#include "Render/GlIncludes.h"
#include <array>
#include <utility>

namespace cutum
{
class UGlPixelUnpackScope
{
public:
  UGlPixelUnpackScope()
  {
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &Buffer);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    for (size_t i = 0; i < Parameters.size(); ++i)
    {
      glGetIntegerv(Parameters[i], &Values[i]);
      glPixelStorei(Parameters[i], i == 0 ? 1 : 0);
    }
  }
  UGlPixelUnpackScope(const UGlPixelUnpackScope &) = delete;
  UGlPixelUnpackScope &operator=(const UGlPixelUnpackScope &) = delete;
  ~UGlPixelUnpackScope()
  {
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, GLuint(Buffer));
    for (size_t i = 0; i < Parameters.size(); ++i)
      glPixelStorei(Parameters[i], Values[i]);
  }

private:
  static constexpr std::array<GLenum, 4> Parameters{
      GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_ROWS, GL_UNPACK_SKIP_PIXELS};
  std::array<GLint, Parameters.size()> Values{};
  GLint Buffer{};
};

// Release and destruction must run on the owning GL context.
class UGlTextureOwner
{
public:
  explicit UGlTextureOwner(GLuint name = 0) noexcept : Name(name) {}
  static UGlTextureOwner Borrow(GLuint name) noexcept
  {
    UGlTextureOwner texture(name);
    texture.Owned = false;
    return texture;
  }
  UGlTextureOwner(const UGlTextureOwner &) = delete;
  UGlTextureOwner &operator=(const UGlTextureOwner &) = delete;
  UGlTextureOwner(UGlTextureOwner &&other) noexcept
      : Name(std::exchange(other.Name, 0)), Owned(other.Owned) {}
  UGlTextureOwner &operator=(UGlTextureOwner &&other) noexcept
  {
    if (this != &other)
    {
      Reset();
      Name = std::exchange(other.Name, 0);
      Owned = other.Owned;
    }
    return *this;
  }
  ~UGlTextureOwner() { Reset(); }

  GLuint Get() const noexcept { return Name; }
  bool IsOwned() const noexcept { return Name && Owned; }
  GLuint Release() noexcept { return std::exchange(Name, 0); }
  void Reset() noexcept
  {
    if (Name && Owned)
      glDeleteTextures(1, &Name);
    Name = 0;
  }

private:
  GLuint Name{};
  bool Owned{true};
};

class UGlFramebufferOwner
{
public:
  UGlFramebufferOwner() { glGenFramebuffers(1, &Name); }
  UGlFramebufferOwner(const UGlFramebufferOwner &) = delete;
  UGlFramebufferOwner &operator=(const UGlFramebufferOwner &) = delete;
  ~UGlFramebufferOwner()
  {
    if (Name)
      glDeleteFramebuffers(1, &Name);
  }
  GLuint Get() const noexcept { return Name; }

private:
  GLuint Name{};
};

// A temporary icon target must detach before its texture can be retired.
class UGlIconColorAttachment
{
public:
  UGlIconColorAttachment(GLuint framebuffer, GLuint texture, GLuint original,
                        GLuint depth = 0, int size = 0, int originalSize = 0)
      : Framebuffer(framebuffer), Original(original), OriginalDepth(depth)
  {
    if (depth && size != originalSize)
    {
      glGenRenderbuffers(1, &TemporaryDepth);
      if (!TemporaryDepth)
        return;
      GLint previous{};
      glGetIntegerv(GL_RENDERBUFFER_BINDING, &previous);
      glBindRenderbuffer(GL_RENDERBUFFER, TemporaryDepth);
      glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);
      const bool allocated = glGetError() == GL_NO_ERROR;
      glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(previous));
      if (!allocated)
        return;
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, Framebuffer);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    if (TemporaryDepth)
      glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, TemporaryDepth);
    Attached = true;
    ReadyToDraw = glGetError() == GL_NO_ERROR;
  }
  UGlIconColorAttachment(const UGlIconColorAttachment &) = delete;
  UGlIconColorAttachment &operator=(const UGlIconColorAttachment &) = delete;
  bool Ready() const noexcept { return ReadyToDraw; }
  ~UGlIconColorAttachment()
  {
    if (Attached)
    {
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, Framebuffer);
      glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, Original, 0);
      if (TemporaryDepth)
        glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, OriginalDepth);
    }
    if (TemporaryDepth)
      glDeleteRenderbuffers(1, &TemporaryDepth);
  }

private:
  GLuint Framebuffer{}, Original{}, OriginalDepth{}, TemporaryDepth{};
  bool Attached{}, ReadyToDraw{};
};
}
