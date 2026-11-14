#ifndef ITEM_LAB_SCENE_RENDERER_H
#define ITEM_LAB_SCENE_RENDERER_H
#include "Items/ItemRenderAsset.h"
#include <memory>

namespace cutum
{
class UItemDefinitionStorage;
class UBlockDefinitionStorage;
class UTextureCubeStorage;
class UCreatureTextureStorage;
class UCreatureDefinitionStorage;
class USkinDefinitionStorage;
class UShaderManager;
class UCreaturePreviewRenderer;
class UFpViewmodelRenderer;
class ItemSurfaceRenderer;
class UItemPreviewRenderer;
struct ItemLabSceneParams
{
  int Scene{0}, Mode{0};
  bool Left{false}, Female{false}, Walk{false};
  float Yaw{35}, Pitch{25}, Time{0}, Ambient{.65f};
  ItemVisualInputs Inputs;
};
/// Studio instances invoke the game's FP, equipment and loose-item drawers.
/// They never take an inventory entry from the controlled actor.
class UItemLabSceneRenderer
{
public:
  UItemLabSceneRenderer(
      std::shared_ptr<UItemDefinitionStorage> items,
      std::shared_ptr<UBlockDefinitionStorage> blocks,
      std::shared_ptr<UTextureCubeStorage> textures,
      std::shared_ptr<UCreatureTextureStorage> creatureTextures,
      std::shared_ptr<UCreatureDefinitionStorage> species,
      std::shared_ptr<USkinDefinitionStorage> skins,
      std::shared_ptr<UShaderManager> shaders);
  ~UItemLabSceneRenderer();
  void Invalidate();
  size_t DrawCount() const { return LastDrawCount; }
  bool SourceMatches() const { return LastSourceMatches; }
  unsigned Render(UItemPreviewRenderer &orbit, const std::string &id,
                  const std::filesystem::path &path,
                  const std::shared_ptr<const ItemRenderAsset> &asset,
                  const ItemLabSceneParams &params);

private:
  size_t LastDrawCount{0};
  bool LastSourceMatches{true};
  std::shared_ptr<UItemDefinitionStorage> Items;
  std::unique_ptr<UCreaturePreviewRenderer> Creature;
  std::unique_ptr<UFpViewmodelRenderer> Fp;
  std::unique_ptr<ItemSurfaceRenderer> Surface;
};
} // namespace cutum
#endif
