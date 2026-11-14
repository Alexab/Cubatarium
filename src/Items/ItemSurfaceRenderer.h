#ifndef ITEM_SURFACE_RENDERER_H
#define ITEM_SURFACE_RENDERER_H

#include "Items/ItemRenderAsset.h"
#include <unordered_map>
#include <unordered_set>

namespace cutum
{
class UShaderManager;
class UShaderProgram;

struct ItemPresentationProfile
{
  glm::vec3 CameraPosition{0.f, 0.f, 3.f};
  glm::vec3 LightDirection{-.4f, .8f, .6f};
  glm::vec3 FogColor{.5f};
  float Ambient{.65f}, Fog{0.f};
  int InspectionMode{0};
};

/// A shared rendering implementation for item presentations; no creature VAOs
/// or shaders participate. Owned GL objects are released before context
/// teardown.
class ItemSurfaceRenderer
{
public:
  ~ItemSurfaceRenderer();
  bool Initialize(UShaderManager &manager);
  void Clear();
  void Draw(const std::shared_ptr<const ItemRenderAsset> &asset,
            const std::filesystem::path &path, const glm::mat4 &viewProjection,
            const glm::mat4 &model, const ItemVisualState &state = {},
            const ItemPresentationProfile &profile = {});

private:
  struct Buffer
  {
    unsigned Vao{0}, Vbo{0}, Ebo{0};
  };
  struct Uploaded
  {
    std::shared_ptr<const ItemRenderAsset> Asset;
    std::vector<Buffer> Primitives;
  };
  std::unordered_map<const ItemRenderAsset *, Uploaded> Geometry;
  std::shared_ptr<UShaderProgram> Shader;
  std::unordered_set<unsigned> FilteredTextures;
};
} // namespace cutum

#endif
