#ifndef ITEM_RENDER_ASSET_H
#define ITEM_RENDER_ASSET_H

#include "Items/ItemVisualInputs.h"
#include <cstdint>
#include <filesystem>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cutum
{
struct VisualAssetStorage;
struct ItemRenderMaterial
{
  std::string Name;
  std::filesystem::path Albedo;
  glm::vec4 Color{1.f};
  glm::vec3 Emissive{0.f};
  float Metallic{0.f}, Roughness{1.f};
};

struct ItemRenderPrimitive
{
  // Item-only vertex format: position xyz, UV xy, normal xyz.
  std::vector<float> Vertices;
  std::vector<unsigned> Indices;
  size_t Material{0}, Node{0};
  glm::vec3 Low{0.f}, High{0.f};
};

struct ItemRenderNode
{
  std::string Name;
  glm::mat4 Transform{1.f};
  glm::mat4 Local{1.f};
  int Parent{-1};
};

struct ItemGripProfile
{
  glm::vec3 RightGrip{0.f}, LeftGrip{0.f};
  glm::vec3 FpEulerDeg{0.f}, FpHandOffset{0.f};
  glm::vec3 TpLeftEulerDeg{0.f}, FpUseEulerDeg{0.f}, FpUseHandOffset{0.f};
  bool HasFpUseHandOffset{false};
  bool HasTpLeftEuler{false}, HasFpUseEuler{false};
  glm::vec3 IconEulerDeg{0.f}, TpEulerDeg{0.f};
  glm::vec3 FpArmEulerDeg{0.f}, GroundEulerDeg{0.f};
  glm::vec3 FpAimEulerDeg{0.f}, TpAimEulerDeg{0.f};
  glm::vec3 CarryEulerDeg{0.f}, CarryRightGrip{0.f}, CarryLeftGrip{0.f};
  bool HasCarryRightGrip{false}, HasCarryLeftGrip{false};
  std::string SupportGripNode;
  size_t SupportGripIndex{size_t(-1)};
  bool HasRightGrip{false}, HasLeftGrip{false}, HasFpEuler{false};
  float FpScale{1.f}, TpScale{1.f};
};

enum class ItemVisualChannel
{
  BowCharge,
  BowLoaded,
  UsePhase,
  FillUnits
};
struct ItemNodeBinding
{
  size_t Node{0};
  ItemVisualChannel Channel{ItemVisualChannel::BowCharge};
  float Minimum{0.f}, Maximum{1.f}, VisibleAbove{-1.f};
  bool Pulse{false}, Span{false};
  glm::vec3 TranslationFrom{0.f}, TranslationTo{0.f};
  glm::vec3 RotationFrom{0.f}, RotationTo{0.f};
  glm::vec3 ScaleFrom{1.f}, ScaleTo{1.f};
  size_t StartNode{0}, EndNode{0};
  glm::vec3 StartPoint{0.f}, EndPoint{0.f};
};

struct ItemWearSection
{
  std::string Node, Region{"slot"};
  std::vector<std::string> Bones;
  size_t Slot{0};
  glm::vec3 Low{0.f}, High{0.f};
  glm::vec3 EulerDeg{0.f};
};

struct ItemStaticWearSection
{
  std::vector<size_t> Primitives;
  glm::vec3 Low{0.f}, High{0.f};
};
struct ItemRenderAsset
{
  std::vector<ItemRenderPrimitive> Primitives;
  std::vector<ItemRenderMaterial> Materials;
  std::vector<ItemRenderNode> Nodes;
  glm::vec3 Low{0.f}, High{0.f};
  ItemGripProfile Grip;
  std::vector<ItemNodeBinding> Bindings;
  std::vector<ItemWearSection> WearSections;
  /// Immutable bind-pose selections prepared by the loader. Animated/hidden
  /// state still takes the full visibility and hierarchy path.
  std::unordered_map<std::string, ItemStaticWearSection> StaticWearSections;
};

/// Strict static item glTF subset. Throws with an actionable reason; callers
/// must not silently replace a malformed production asset with a cube.
std::shared_ptr<ItemRenderAsset>
LoadItemRenderAsset(const std::filesystem::path &path,
                    bool legacyPreviewNormals = false);
std::shared_ptr<const ItemRenderAsset>
FindItemSurfaceAsset(const std::filesystem::path &path, uint64_t revision = 0);
void ClearItemSurfaceAssets();
VisualAssetStorage InspectItemSurfaceAssetStorage();
struct ItemAssetLookupStats
{
  uint64_t FastHits{0}, CanonicalResolutions{0};
};
ItemAssetLookupStats GetItemAssetLookupStats();
glm::mat4 ApplyItemHandProfile(const glm::mat4 &root, const glm::vec3 &socket,
                               const ItemGripProfile &profile, bool left,
                               float usePhase = 0.f);
struct ItemVisualState
{
  std::unordered_map<std::string, glm::mat4> NodeTransforms;
  std::unordered_set<std::string> HiddenNodes;
  std::unordered_set<std::string> VisibleRoots;
};
ItemVisualState EvaluateItemVisualState(const ItemRenderAsset &asset,
                                        const ItemVisualInputs &inputs);
bool ItemNodeVisible(const ItemRenderAsset &asset, const ItemVisualState &state,
                     size_t node);
std::pair<glm::vec3, glm::vec3> ItemVisualBounds(const ItemRenderAsset &asset,
                                                 const ItemVisualState &state);
std::vector<glm::mat4> ResolveItemNodeTransforms(const ItemRenderAsset &asset,
                                                 const ItemVisualState &state);
} // namespace cutum

#endif
