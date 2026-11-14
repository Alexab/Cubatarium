#ifndef ITEM_RENDER_ASSET_H
#define ITEM_RENDER_ASSET_H

#include <filesystem>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cutum
{
struct ItemRenderMaterial
{
  std::string Name;
  std::filesystem::path Albedo;
  glm::vec4 Color{1.f};
  float Metallic{0.f}, Roughness{1.f};
};

struct ItemRenderPrimitive
{
  // Item-only vertex format: position xyz, UV xy, normal xyz.
  std::vector<float> Vertices;
  std::vector<unsigned> Indices;
  size_t Material{0}, Node{0};
};

struct ItemRenderNode
{
  std::string Name;
  glm::mat4 Transform{1.f};
  glm::mat4 Local{1.f};
  int Parent{-1};
};

struct ItemRenderAsset
{
  std::vector<ItemRenderPrimitive> Primitives;
  std::vector<ItemRenderMaterial> Materials;
  std::vector<ItemRenderNode> Nodes;
  glm::vec3 Low{0.f}, High{0.f};
};

/// Strict static item glTF subset. Throws with an actionable reason; callers
/// must not silently replace a malformed production asset with a cube.
std::shared_ptr<ItemRenderAsset>
LoadItemRenderAsset(const std::filesystem::path &path);
std::shared_ptr<const ItemRenderAsset>
FindItemSurfaceAsset(const std::filesystem::path &path);
void ClearItemSurfaceAssets();
struct ItemVisualState
{
  std::unordered_map<std::string, glm::mat4> NodeTransforms;
};
std::vector<glm::mat4> ResolveItemNodeTransforms(const ItemRenderAsset &asset,
                                                 const ItemVisualState &state);
} // namespace cutum

#endif
