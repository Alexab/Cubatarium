#pragma once

#include "App/Platform/Log.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "Render/GlIncludes.h"
#include "Render/Textures/TextureCube.h"
#include "World/Core/World.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <map>
#include <nlohmann/json.hpp>
#include <vector>

namespace cutum
{
// Two bounded atlas reads per opt-in framebuffer capture. No render inputs are
// changed. The witness describes the actual uploaded atlas, not just pack
// intent.
inline void
CaptureRgbReferenceMaterials(UWorld &world,
                             const std::map<size_t, UTextureCube> &textures,
                             uint64_t frame_epoch)
{
  const char *enabled = std::getenv("CUBA_RGB_REFERENCE");
  if (!enabled || std::string(enabled) != "1")
    return;
  const auto catalog = world.GetBlockRegistry().GetDefinitionsCatalogSnapshot();
  for (const char *name : {"stone", "glowstone"})
  {
    auto &registry = world.GetBlockRegistry();
    const auto id = registry.GetIdByTypeName(name);
    const auto entry = textures.find(size_t(id));
    nlohmann::json record = {{"schema", "rgb-reference-material.v1"},
                             {"frame_epoch", frame_epoch},
                             {"material_id", id},
                             {"name", name},
                             {"world_folder", world.GetWorldFolderPath()},
                             {"valid", false}};
    // Keep the immutable catalog alive through all properties sampled below.
    const BlockDefinition *properties = nullptr;
    if (catalog)
    {
      const auto found = catalog->ById.find(id);
      if (found != catalog->ById.end())
        properties = &found->second;
    }
    if (properties && entry != textures.end() &&
        entry->second.GetTextureId() != 0)
    {
      record["variation"] = properties->Render.TextureVariation;
      record["emission"] = registry.GetLightEmission(id);
      record["opaque_cube"] =
          !registry.IsTransparent(id) &&
          registry.GetRenderStyle(id) == BlockRenderStyle::UCube;
      record["frames"] = entry->second.GetNumTextureFrames();
      record["stems"] = entry->second.GetTextureNames();
      record["texture_id"] = entry->second.GetTextureId();
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
      GLint previous_texture = 0, previous_pack_buffer = 0;
      glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
      glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previous_pack_buffer);
      constexpr std::array<GLenum, 6> pack_names = {
          GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH,   GL_PACK_SKIP_PIXELS,
          GL_PACK_SKIP_ROWS, GL_PACK_IMAGE_HEIGHT, GL_PACK_SKIP_IMAGES};
      std::array<GLint, 6> previous_pack{};
      for (size_t i = 0; i < pack_names.size(); ++i)
      {
        glGetIntegerv(pack_names[i], &previous_pack[i]);
        glPixelStorei(pack_names[i], i == 0 ? 1 : 0);
      }
      glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
      glBindTexture(GL_TEXTURE_2D, entry->second.GetTextureId());
      GLint width = 0, height = 0, format = 0;
      glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
      glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
      glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT,
                               &format);
      record["width"] = width;
      record["height"] = height;
      record["linear_texture"] = format == GL_RGBA || format == GL_RGBA8;
      record["framebuffer_srgb"] = glIsEnabled(GL_FRAMEBUFFER_SRGB) == GL_TRUE;
      const uint64_t count =
          width > 0 && height > 0 ? uint64_t(width) * uint64_t(height) : 0;
      if (count > 0 && count <= 65536)
      {
        std::vector<GLubyte> pixels(size_t(count) * 4);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                      pixels.data());
        const auto error = glGetError();
        std::array<int, 4> minimum{255, 255, 255, 255}, maximum{0, 0, 0, 0};
        for (size_t i = 0; i < pixels.size(); ++i)
        {
          minimum[i % 4] = std::min(minimum[i % 4], int(pixels[i]));
          maximum[i % 4] = std::max(maximum[i % 4], int(pixels[i]));
        }
        record["rgba_min"] = minimum;
        record["rgba_max"] = maximum;
        record["valid"] = error == GL_NO_ERROR;
        record["gl_error"] = error;
      }
      glBindTexture(GL_TEXTURE_2D, previous_texture);
      glBindBuffer(GL_PIXEL_PACK_BUFFER, previous_pack_buffer);
      for (size_t i = 0; i < pack_names.size(); ++i)
        glPixelStorei(pack_names[i], previous_pack[i]);
#endif
    }
    CubatariumLogInfo("RgbReferenceMaterial", record.dump());
  }
}
} // namespace cutum
