#pragma once
#include "Items/ItemRenderAsset.h"
#include <functional>
#include <nlohmann/json_fwd.hpp>

namespace cutum
{
struct ItemLabTextureMetrics
{
  std::filesystem::path Path;
  glm::ivec2 Size{0};
  uint64_t Levels{0}, RgbaMipBytes{0}, EncodedBytes{0};
};
struct ItemLabResourceMetrics
{
  uint64_t Vertices{0}, Triangles{0}, Primitives{0}, Materials{0};
  uint64_t GeometryBytes{0}, RgbaMipBytes{0};
  std::vector<ItemLabTextureMetrics> Textures;
};
using ItemLabImageProbe =
    std::function<glm::ivec2(const std::filesystem::path &)>;
/// CPU buffer payload and unique referenced images, not process/driver VRAM.
ItemLabResourceMetrics MeasureItemLabResources(const ItemRenderAsset &asset,
                                               const ItemLabImageProbe &probe);
nlohmann::json ItemLabMetricsJson(const ItemLabResourceMetrics &metrics);
struct ItemLabLineVertex
{
  glm::vec3 Position{0};
  glm::vec4 Color{1};
};
std::vector<ItemLabLineVertex> ItemLabContactLines(const ItemRenderAsset &asset,
                                                   const ItemVisualState &state,
                                                   glm::vec3 physicalSize);
/// Only edges belonging to the selected albedo; triangle index/UV data are
/// real.
std::vector<ItemLabLineVertex>
ItemLabUvLines(const ItemRenderAsset &asset,
               const std::filesystem::path &albedo);
} // namespace cutum
