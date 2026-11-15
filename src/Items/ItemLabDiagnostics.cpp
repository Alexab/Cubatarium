#include "Items/ItemLabDiagnostics.h"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

namespace cutum
{
ItemLabResourceMetrics MeasureItemLabResources(const ItemRenderAsset &asset,
                                               const ItemLabImageProbe &probe)
{
  ItemLabResourceMetrics result;
  result.Primitives = asset.Primitives.size();
  result.Materials = asset.Materials.size();
  std::set<std::filesystem::path> images;
  for (const auto &primitive : asset.Primitives)
  {
    result.Vertices += primitive.Vertices.size() / 8;
    result.Triangles += primitive.Indices.size() / 3;
    result.GeometryBytes += primitive.Vertices.size() * sizeof(float) +
                            primitive.Indices.size() * sizeof(unsigned);
    if (primitive.Material >= asset.Materials.size())
      throw std::runtime_error("Item Lab: invalid primitive material");
    images.insert(
        asset.Materials[primitive.Material].Albedo.lexically_normal());
  }
  for (const auto &path : images)
  {
    ItemLabTextureMetrics image;
    image.Path = path;
    image.Size = probe(path);
    if (glm::any(glm::lessThanEqual(image.Size, glm::ivec2(0))) ||
        glm::any(glm::greaterThan(image.Size, glm::ivec2(1 << 20))))
      throw std::runtime_error("Item Lab: invalid image dimensions: " +
                               path.string());
    auto size = image.Size;
    for (;;)
    {
      image.RgbaMipBytes += uint64_t(size.x) * uint64_t(size.y) * 4;
      ++image.Levels;
      if (size.x == 1 && size.y == 1)
        break;
      size = glm::max(size / 2, glm::ivec2(1));
    }
    std::error_code error;
    image.EncodedBytes = std::filesystem::file_size(path, error);
    if (error)
      throw std::runtime_error("Item Lab: cannot measure image: " +
                               path.string());
    result.RgbaMipBytes += image.RgbaMipBytes;
    result.Textures.push_back(std::move(image));
  }
  return result;
}
nlohmann::json ItemLabMetricsJson(const ItemLabResourceMetrics &m)
{
  nlohmann::json result = {
      {"vertices", m.Vertices},
      {"triangles", m.Triangles},
      {"primitives", m.Primitives},
      {"materials", m.Materials},
      {"geometry_buffer_bytes", m.GeometryBytes},
      {"rgba8_mip_bytes", m.RgbaMipBytes},
      {"scope", "one item renderer buffer payload and referenced RGBA8 mip "
                "texels; not driver VRAM"},
      {"textures", nlohmann::json::array()}};
  for (const auto &image : m.Textures)
    result["textures"].push_back({{"path", image.Path.generic_string()},
                                  {"width", image.Size.x},
                                  {"height", image.Size.y},
                                  {"levels", image.Levels},
                                  {"rgba8_mip_bytes", image.RgbaMipBytes},
                                  {"encoded_bytes", image.EncodedBytes}});
  return result;
}
std::vector<ItemLabLineVertex> ItemLabContactLines(const ItemRenderAsset &asset,
                                                   const ItemVisualState &state,
                                                   glm::vec3 physicalSize)
{
  std::vector<ItemLabLineVertex> result;
  auto line = [&](glm::vec3 a, glm::vec3 b, glm::vec4 color)
  {
    result.push_back({a, color});
    result.push_back({b, color});
  };
  const auto half = physicalSize * .5f;
  const glm::vec4 box(.85f, .35f, 1.f, 1);
  for (int axis = 0; axis < 3; ++axis)
    for (int a : {-1, 1})
      for (int b : {-1, 1})
      {
        glm::vec3 from(0), to(0);
        from[axis] = -half[axis];
        to[axis] = half[axis];
        from[(axis + 1) % 3] = to[(axis + 1) % 3] = a * half[(axis + 1) % 3];
        from[(axis + 2) % 3] = to[(axis + 2) % 3] = b * half[(axis + 2) % 3];
        line(from, to, box);
      }
  const float marker = std::max(.008f, glm::length(physicalSize) * .025f);
  auto cross = [&](glm::vec3 point, glm::vec4 color, bool diagonal = false)
  {
    for (int axis = 0; axis < 3; ++axis)
    {
      glm::vec3 offset(0);
      offset[axis] = marker;
      if (diagonal)
      {
        offset[(axis + 1) % 3] = marker;
        offset *= .7f;
      }
      line(point - offset, point + offset, color);
    }
  };
  if (asset.Grip.HasRightGrip)
    cross(asset.Grip.RightGrip, {1, .2f, .15f, 1});
  if (asset.Grip.HasLeftGrip)
    cross(asset.Grip.LeftGrip, {.1f, 1, 1, 1}, true);
  if (!asset.Grip.SupportGripNode.empty() &&
      asset.Grip.SupportGripIndex < asset.Nodes.size())
  {
    const auto nodes = ResolveItemNodeTransforms(asset, state);
    const auto support = glm::vec3(nodes[asset.Grip.SupportGripIndex][3]);
    cross(support, {1, .8f, .15f, 1});
    if (asset.Grip.HasRightGrip)
      line(asset.Grip.RightGrip, support, {1, .8f, .15f, 1});
  }
  return result;
}
std::vector<ItemLabLineVertex>
ItemLabUvLines(const ItemRenderAsset &asset,
               const std::filesystem::path &albedo)
{
  std::vector<ItemLabLineVertex> result;
  for (const auto &primitive : asset.Primitives)
  {
    if (primitive.Material >= asset.Materials.size() ||
        asset.Materials[primitive.Material].Albedo.lexically_normal() !=
            albedo.lexically_normal())
      continue;
    for (size_t i = 0; i + 2 < primitive.Indices.size(); i += 3)
      for (size_t edge = 0; edge < 3; ++edge)
        for (size_t end : {edge, (edge + 1) % 3})
        {
          const auto index = size_t(primitive.Indices[i + end]) * 8;
          if (index + 4 >= primitive.Vertices.size())
            throw std::runtime_error("Item Lab: invalid UV index");
          result.push_back({{primitive.Vertices[index + 3],
                             primitive.Vertices[index + 4], 0},
                            {.35f, 1, .35f, 1}});
        }
  }
  return result;
}
} // namespace cutum
