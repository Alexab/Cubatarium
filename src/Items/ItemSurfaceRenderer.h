#ifndef ITEM_SURFACE_RENDERER_H
#define ITEM_SURFACE_RENDERER_H

#include "Items/ItemRenderAsset.h"
#include <optional>
#include <unordered_map>
#include <vector>

namespace cutum
{
class UShaderManager;
class UShaderProgram;

struct ItemSurfaceDrawAudit
{
  std::filesystem::path Path;
  std::shared_ptr<const ItemRenderAsset> Asset;
  size_t VisiblePrimitives{0};
  glm::mat4 Model{1.f};
};
/// Opt-in diagnostics scoped to a render pass; nested previews restore the
/// caller's collector. Geometry ownership remains with the renderer.
class UItemSurfaceDrawAuditScope
{
public:
  explicit UItemSurfaceDrawAuditScope(std::vector<ItemSurfaceDrawAudit> *output)
      : Previous(Current)
  {
    Current = output;
  }
  ~UItemSurfaceDrawAuditScope() { Current = Previous; }
  UItemSurfaceDrawAuditScope(const UItemSurfaceDrawAuditScope &) = delete;
  UItemSurfaceDrawAuditScope &
  operator=(const UItemSurfaceDrawAuditScope &) = delete;
  static inline thread_local std::vector<ItemSurfaceDrawAudit> *Current{
      nullptr};

private:
  std::vector<ItemSurfaceDrawAudit> *Previous;
};

struct ItemPresentationProfile
{
  glm::vec3 CameraPosition{0.f, 0.f, 3.f};
  glm::vec3 LightDirection{-.4f, .8f, .6f};
  glm::vec3 FogColor{.5f};
  float Ambient{.65f}, Fog{0.f}, LightIntensity{1.f};
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
  /// Number of nonempty primitive draw calls actually issued.
  size_t Draw(const std::shared_ptr<const ItemRenderAsset> &asset,
              const std::filesystem::path &path,
              const glm::mat4 &viewProjection, const glm::mat4 &model,
              const ItemVisualState &state = {},
              const ItemPresentationProfile &profile = {});

private:
  friend class UItemSurfaceBatchScope;
  bool BatchActive{false};
  std::optional<ItemPresentationProfile> BatchProfile;
  struct MaterialUniforms
  {
    glm::vec4 Color{1.f};
    glm::vec3 Emissive{0.f};
    float Metallic{0.f}, Roughness{1.f};
  };
  std::optional<MaterialUniforms> BatchMaterial;
  struct Buffer
  {
    unsigned Vao{0}, Vbo{0}, Ebo{0};
  };
  struct Uploaded
  {
    std::shared_ptr<const ItemRenderAsset> Asset;
    Buffer GeometryBuffer;
    std::vector<size_t> IndexByteOffsets;
  };
  std::unordered_map<const ItemRenderAsset *, Uploaded> Geometry;
  std::shared_ptr<UShaderProgram> Shader;
};
/// Only scopes a contiguous queue owned by this renderer. Other renderers may
/// share the shader, so parameter caches never survive this scope.
class UItemSurfaceBatchScope
{
public:
  explicit UItemSurfaceBatchScope(ItemSurfaceRenderer &renderer);
  ~UItemSurfaceBatchScope();
  UItemSurfaceBatchScope(const UItemSurfaceBatchScope &) = delete;
  UItemSurfaceBatchScope &operator=(const UItemSurfaceBatchScope &) = delete;

private:
  ItemSurfaceRenderer &Renderer;
};
} // namespace cutum

#endif
