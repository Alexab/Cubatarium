#ifndef CREATURE_PREVIEW_LAYOUT_H
#define CREATURE_PREVIEW_LAYOUT_H

#include "Creatures/Core/CreatureCatalogTypes.h"
#include "Creatures/Visual/Gltf/CreatureGltfTypes.h"
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

struct CreaturePreviewFit
{
  glm::vec3 center{0.0f};
  float uniformScale{1.0f};
};

CreaturePreviewFit
FitCreaturePreview(const std::vector<ResolvedCreaturePart> &parts,
                   float targetSpan = 1.5f);

glm::mat4 CreaturePreviewPartModel(const CreaturePreviewFit &fit,
                                   const ResolvedCreaturePart &part);

glm::mat4 GltfPreviewRootMatrix(const CreatureGltfMeshAsset &asset,
                                float targetSpan = 1.2f,
                                float feetOffsetY = 0.f);

} // namespace cutum

#endif
