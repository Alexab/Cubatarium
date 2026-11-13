#ifndef CREATUREDRAWCONTEXT_H
#define CREATUREDRAWCONTEXT_H

#include <glm/glm.hpp>

namespace cutum
{

class CreatureDrawQueue;
class UCreatureTextureStorage;
struct RenderSettings;
class UItemDefinitionStorage;

/// Everything a visual backend needs to submit draw requests — a narrow
/// replacement for handing backends the whole UGeometryEngine (audit V13).
struct CreatureDrawContext
{
  CreatureDrawQueue *Queue{nullptr};
  const UCreatureTextureStorage *Textures{nullptr};
  const RenderSettings *Settings{nullptr};
  /// World-space camera position, for camera-facing billboards. The sprite
  /// backend used to reach World->Camera from inside Visual (audit V9).
  glm::vec3 CameraPosition{0.f};
  /// Item definitions for worn-equipment rendering (world path).
  const UItemDefinitionStorage *Items{nullptr};
  bool SkipWieldedItems{false}; // Preview supplies its own item overlay.
};

} // namespace cutum

#endif
