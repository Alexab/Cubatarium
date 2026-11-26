#pragma once

#include "Render/GlIncludes.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <string_view>
#include <utility>
#include <vector>

namespace cutum
{
enum class RenderImageRole
{
  LightAtlas, LightMetadata, OpaqueDepth, WeatherDepth, FluidSurfaces, CloudField,
  ContentColor, ContentDepth, ItemPreviewColor, ItemPreviewDepth, ItemLabColor,
  BlockAtlas, CreatureTexture, ModelTexture, ItemAlbedo, ObjectIcon, BlockIcon,
  CreatureIcon, SkinIcon, ItemIcon, IconTargetColor, IconTargetDepth, TextGlyph,
  CreaturePreviewColor, CreaturePreviewDepth, Count
};

inline constexpr std::array RenderImageRoles{
    "light_atlas", "light_metadata", "opaque_depth", "weather_depth", "fluid_surfaces", "cloud_field",
    "content_color", "content_depth", "item_preview_color", "item_preview_depth", "item_lab_color",
    "block_atlas", "creature_texture", "model_texture", "item_albedo", "object_icon", "block_icon",
    "creature_icon", "skin_icon", "item_icon", "icon_target_color", "icon_target_depth", "text_glyph",
    "creature_preview_color", "creature_preview_depth"};
static_assert(RenderImageRoles.size() == static_cast<std::size_t>(RenderImageRole::Count));
static_assert(RenderImageRoles.size() <= std::numeric_limits<std::uint32_t>::digits);

enum class RenderTextureLevels { LevelZero, Allocated };

struct RenderImageLevel
{
  GLint level{}, width{-1}, height{-1}, depth{1}, samples{1}, format{-1};
  std::uint64_t bitsPerSample{}, logicalBytes{};
  bool compressed{};
};

struct RenderImageStorage
{
  GLuint name{};
  GLenum target{};
  std::uint32_t roles{};
  std::vector<RenderImageLevel> levels;
  std::uint64_t logicalBytes{};
};

struct RenderImageSnapshot
{
  std::vector<RenderImageStorage> images;
  std::uint64_t logicalBytes{};
  std::size_t references{}, namesWithoutObject{}, imagesWithoutStorage{}, failedQueries{};
  bool contextAvailable{}, overflow{};
};

// Selected images in the current context. Asset sources can ask for every
// allocated level, including levels outside the current sampler's range.
// Texture and renderbuffer names have independent namespaces. A texture shared
// through several owners contributes its logical storage once.
class RenderImageCensus
{
public:
  void Add(RenderImageRole role, GLenum target, GLuint name,
           RenderTextureLevels levels = RenderTextureLevels::LevelZero)
  {
    if (!name)
      return;
    auto &image = Names[{target == GL_RENDERBUFFER, name}];
    const auto mask = std::uint32_t{1} << static_cast<std::size_t>(role);
    if (!image.roles)
      image.target = target == GL_TEXTURE_2D || target == GL_TEXTURE_3D || target == GL_RENDERBUFFER ? target : 0;
    else if (image.target != target)
      image.target = 0;
    image.roles |= mask;
    image.allLevels |= levels == RenderTextureLevels::Allocated;
    ++References;
  }

  RenderImageSnapshot Read() const
  {
    RenderImageSnapshot result;
    result.references = References;
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
    if (!glIsRenderbuffer || !glBindRenderbuffer || !glGetRenderbufferParameteriv)
      return result;
#endif
    const auto *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
    if (!version)
      return result;
    const bool gles = std::string_view(version).substr(0, 9) == "OpenGL ES";
    if (gles)
    {
      GLint major{}, minor{};
      glGetIntegerv(GL_MAJOR_VERSION, &major);
      glGetIntegerv(GL_MINOR_VERSION, &minor);
      if (major < 3 || (major == 3 && minor < 1))
        return result;
    }
    result.contextAvailable = true;
    result.images.reserve(Names.size());
    const auto maxLevel = [](GLenum parameter) {
      GLint limit{}, level{};
      glGetIntegerv(parameter, &limit);
      if (limit <= 0)
        return -1;
      for (; limit > 1; limit /= 2)
        ++level;
      return level;
    };
    const GLint max2d = maxLevel(GL_MAX_TEXTURE_SIZE);
    const GLint max3d = maxLevel(GL_MAX_3D_TEXTURE_SIZE);
    for (const auto &[key, source] : Names)
    {
      RenderImageStorage image;
      image.name = key.second;
      image.target = source.target;
      image.roles = source.roles;
      if (!image.target)
      {
        ++result.failedQueries;
        continue;
      }
      if (!(key.first ? glIsRenderbuffer(image.name) : glIsTexture(image.name)))
      {
        ++result.namesWithoutObject;
        continue;
      }
      const GLint highest = source.allLevels && !key.first ?
          (image.target == GL_TEXTURE_3D ? max3d : max2d) : 0;
      if (highest < 0 || !ReadParameters(image, highest, gles, result.overflow))
      {
        ++result.failedQueries;
        continue;
      }
      if (image.levels.empty())
      {
        ++result.imagesWithoutStorage;
        continue;
      }
      if (image.logicalBytes > std::numeric_limits<std::uint64_t>::max() - result.logicalBytes)
      {
        result.overflow = true;
        continue;
      }
      result.logicalBytes += image.logicalBytes;
      result.images.push_back(std::move(image));
    }
    return result;
  }

private:
  template<class Query> static bool ReadComponentBits(RenderImageLevel &image, Query query,
                                                       const std::array<GLenum, 7> &components)
  {
    for (const auto component : components)
    {
      if (!component)
        continue;
      const auto bits = query(component);
      if (bits < 0)
        return false;
      image.bitsPerSample += static_cast<std::uint64_t>(bits);
    }
    return image.bitsPerSample != 0;
  }

  static bool ReadParameters(RenderImageStorage &image, GLint highest, bool gles, bool &overflow)
  {
    const bool renderbuffer = image.target == GL_RENDERBUFFER;
    const GLenum binding = renderbuffer ? GL_RENDERBUFFER_BINDING :
        image.target == GL_TEXTURE_3D ? GL_TEXTURE_BINDING_3D : GL_TEXTURE_BINDING_2D;
    GLint prior{};
    glGetIntegerv(binding, &prior);
    struct RestoreBinding
    {
      GLenum target;
      GLuint name;
      void Bind(GLuint next) const
      {
        if (target == GL_RENDERBUFFER)
          glBindRenderbuffer(target, next);
        else
          glBindTexture(target, next);
      }
      ~RestoreBinding() { Bind(name); }
    } restore{image.target, static_cast<GLuint>(prior)};
    restore.Bind(image.name);
    GLint bound{};
    glGetIntegerv(binding, &bound);
    if (static_cast<GLuint>(bound) != image.name)
      return false;
    for (GLint level = 0; level <= highest; ++level)
    {
      const auto query = [&](GLenum property) {
        GLint value = -1;
        if (renderbuffer)
          glGetRenderbufferParameteriv(GL_RENDERBUFFER, property, &value);
        else
          glGetTexLevelParameteriv(image.target, level, property, &value);
        return value;
      };
      RenderImageLevel extent;
      extent.level = level;
      extent.width = query(renderbuffer ? GL_RENDERBUFFER_WIDTH : GL_TEXTURE_WIDTH);
      extent.height = query(renderbuffer ? GL_RENDERBUFFER_HEIGHT : GL_TEXTURE_HEIGHT);
      if (extent.width < 0 || extent.height < 0)
        return false;
      if (!extent.width || !extent.height)
        continue;
      if (!ReadLevel(extent, image.target, gles, query))
        return false;
      if (!extent.logicalBytes)
      {
        auto bits = extent.bitsPerSample;
        for (const auto factor : {extent.width, extent.height, extent.depth, extent.samples})
        {
          if (bits > std::numeric_limits<std::uint64_t>::max() / static_cast<std::uint64_t>(factor))
          {
            overflow = true;
            return false;
          }
          bits *= static_cast<std::uint64_t>(factor);
        }
        extent.logicalBytes = bits / 8 + (bits % 8 != 0);
      }
      if (extent.logicalBytes > std::numeric_limits<std::uint64_t>::max() - image.logicalBytes)
      {
        overflow = true;
        return false;
      }
      image.logicalBytes += extent.logicalBytes;
      image.levels.push_back(extent);
    }
    return true;
  }

  template<class Query> static bool ReadLevel(RenderImageLevel &image, GLenum target, bool gles, Query query)
  {
    const bool renderbuffer = target == GL_RENDERBUFFER;
    image.format = query(renderbuffer ? GL_RENDERBUFFER_INTERNAL_FORMAT : GL_TEXTURE_INTERNAL_FORMAT);
    if (image.format <= 0)
      return false;
    if (renderbuffer)
    {
      const auto samples = query(GL_RENDERBUFFER_SAMPLES);
      if (samples < 0)
        return false;
      image.samples = std::max(1, samples);
      return ReadComponentBits(image, query, {GL_RENDERBUFFER_RED_SIZE, GL_RENDERBUFFER_GREEN_SIZE,
          GL_RENDERBUFFER_BLUE_SIZE, GL_RENDERBUFFER_ALPHA_SIZE, GL_RENDERBUFFER_DEPTH_SIZE,
          GL_RENDERBUFFER_STENCIL_SIZE, 0});
    }
    if (target == GL_TEXTURE_3D)
      image.depth = query(GL_TEXTURE_DEPTH);
    const auto compressed = query(GL_TEXTURE_COMPRESSED);
    if (image.depth <= 0 || (compressed != GL_FALSE && compressed != GL_TRUE))
      return false;
    if (compressed == GL_TRUE)
    {
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
      if (!gles)
      {
        const auto bytes = query(GL_TEXTURE_COMPRESSED_IMAGE_SIZE);
        if (bytes > 0)
        {
          image.compressed = true;
          image.logicalBytes = static_cast<std::uint64_t>(bytes);
          return true;
        }
      }
#endif
      return false;
    }
    (void)gles;
    return ReadComponentBits(image, query, {GL_TEXTURE_RED_SIZE, GL_TEXTURE_GREEN_SIZE,
        GL_TEXTURE_BLUE_SIZE, GL_TEXTURE_ALPHA_SIZE, GL_TEXTURE_DEPTH_SIZE, GL_TEXTURE_STENCIL_SIZE,
        GL_TEXTURE_SHARED_SIZE});
  }

  struct Source
  {
    GLenum target{};
    std::uint32_t roles{};
    bool allLevels{};
  };
  std::map<std::pair<bool, GLuint>, Source> Names;
  std::size_t References{};
};
} // namespace cutum
