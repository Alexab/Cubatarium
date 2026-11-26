#pragma once

#include "Core/Memory/StbiAllocation.h"
#include "Render/Textures/VisualTexture.h"
#include "ThirdParty/stb_image.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>

namespace cutum
{

struct VisualTextureResult
{
  VisualTexture Image;
  bool Retry{};
};

enum class VisualTextureSampling { Nearest, Linear, SrgbMipmapped };

struct VisualTextureUploadProbe
{
  void *State{};
  void (*Record)(void *, GLuint, size_t) noexcept {};
};

class UVisualTextureUploadProbeScope
{
public:
  explicit UVisualTextureUploadProbeScope(const VisualTextureUploadProbe &probe) noexcept
      : Previous(std::exchange(Current, &probe)) {}
  UVisualTextureUploadProbeScope(const UVisualTextureUploadProbeScope &) = delete;
  UVisualTextureUploadProbeScope &operator=(const UVisualTextureUploadProbeScope &) = delete;
  ~UVisualTextureUploadProbeScope() { Current = Previous; }

  static void Observe(const VisualTexture &texture) noexcept
  {
    if (Current && Current->Record)
      Current->Record(Current->State, texture.Name.Get(), texture.RgbaBytes);
  }
  static constexpr size_t ThreadInlineBytes() noexcept { return sizeof(Current); }

private:
  const VisualTextureUploadProbe *Previous;
  static inline thread_local const VisualTextureUploadProbe *Current{};
};

class RestoreTextureBinding
{
public:
  RestoreTextureBinding() { glGetIntegerv(GL_TEXTURE_BINDING_2D, &Previous); }
  RestoreTextureBinding(const RestoreTextureBinding &) = delete;
  RestoreTextureBinding &operator=(const RestoreTextureBinding &) = delete;
  ~RestoreTextureBinding() { glBindTexture(GL_TEXTURE_2D, GLuint(Previous)); }

private:
  GLint Previous{};
};

inline bool FitsVisualTexture(GLint width, GLint height)
{
  GLint limit = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
  return width > 0 && height > 0 && width <= limit && height <= limit &&
         size_t(width) <= SIZE_MAX / 4 / size_t(height);
}

inline VisualTextureResult UploadVisualTexture(const stbi_uc *pixels, int width, int height,
                                               VisualTextureSampling sampling)
{
  if (!pixels || !FitsVisualTexture(width, height))
    return {};
  GLuint name = 0;
  glGenTextures(1, &name);
  VisualTexture image{UGlTextureOwner(name), 0};
  if (!name)
    return {VisualTexture{}, true};
  RestoreTextureBinding binding;
  const bool mipmapped = sampling == VisualTextureSampling::SrgbMipmapped;
  const GLint filter = sampling == VisualTextureSampling::Nearest ? GL_NEAREST : GL_LINEAR;
  glBindTexture(GL_TEXTURE_2D, name);
  {
    UGlPixelUnpackScope unpack;
    glTexImage2D(GL_TEXTURE_2D, 0, mipmapped ? GL_SRGB8_ALPHA8 : GL_RGBA,
                 width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  }
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmapped ? GL_LINEAR_MIPMAP_LINEAR : filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  for (int level = 0;; ++level)
  {
    GLint actualWidth = 0, actualHeight = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_WIDTH, &actualWidth);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_HEIGHT, &actualHeight);
    if (actualWidth != width || actualHeight != height)
      return {VisualTexture{}, true};
    const size_t bytes = size_t(width) * size_t(height) * 4;
    if (bytes > SIZE_MAX - image.RgbaBytes)
      return {};
    image.RgbaBytes += bytes;
    if (!mipmapped || (width == 1 && height == 1))
      break;
    if (level == 0)
      glGenerateMipmap(GL_TEXTURE_2D);
    width = std::max(1, width / 2);
    height = std::max(1, height / 2);
  }
  UVisualTextureUploadProbeScope::Observe(image);
  return {std::move(image), false};
}

inline VisualTextureResult DecodeVisualTextureFile(const std::filesystem::path &path,
                                                  VisualTextureSampling sampling)
{
  int width = 0, height = 0, channels = 0;
  const auto generation = GetStbiThreadFailureGeneration();
  std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(nullptr, stbi_image_free);
  {
#if defined(_WIN32)
    auto *open = _wfopen(path.c_str(), L"rb");
#else
    auto *open = std::fopen(path.c_str(), "rb");
#endif
    const std::unique_ptr<std::FILE, decltype(&std::fclose)> file(open, std::fclose);
    if (file && stbi_info_from_file(file.get(), &width, &height, &channels) && FitsVisualTexture(width, height))
      pixels.reset(stbi_load_from_file(file.get(), &width, &height, &channels, 4));
  }
  if (!pixels)
    return {VisualTexture{}, generation != GetStbiThreadFailureGeneration()};
  return UploadVisualTexture(pixels.get(), width, height, sampling);
}

inline VisualTextureResult DecodeVisualTextureMemory(const stbi_uc *data, int size,
                                                    VisualTextureSampling sampling)
{
  int width = 0, height = 0, channels = 0;
  const auto generation = GetStbiThreadFailureGeneration();
  std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(nullptr, stbi_image_free);
  if (data && size > 0 && stbi_info_from_memory(data, size, &width, &height, &channels) && FitsVisualTexture(width, height))
    pixels.reset(stbi_load_from_memory(data, size, &width, &height, &channels, 4));
  if (!pixels)
    return {VisualTexture{}, generation != GetStbiThreadFailureGeneration()};
  return UploadVisualTexture(pixels.get(), width, height, sampling);
}

} // namespace cutum
