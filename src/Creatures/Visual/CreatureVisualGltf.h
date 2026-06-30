#ifndef CREATUREVISUALGLTF_H
#define CREATUREVISUALGLTF_H

#include "Creatures/Visual/CreatureVisual.h"
#include "Creatures/Visual/Gltf/CreatureGltfTypes.h"
#include <memory>
#include <string>

namespace cutum
{

std::unique_ptr<ICreatureVisual> CreateCreatureVisualGltf();

class UCreatureVisualGltf : public ICreatureVisual
{
public:
  void UpdatePose(const UCreature &creature,
                  const CreatureLocomotionFacts &facts,
                  const CreaturePoseParams &pose,
                  const CreatureDefinition &animDef, float dt) override;
  void SubmitDraw(UGeometryEngine &engine, const glm::mat4 &viewProj) override;

private:
  std::shared_ptr<CreatureGltfMeshAsset> MeshAsset;
  glm::mat4 RootAnimMatrix{1.f};
  glm::vec3 BodyOrigin{0.f};
  float BodyYaw{0.f};
  float ModelScale{1.f};
  float ModelFeetOffsetY{0.f};
  std::string SpeciesId;
  std::string DefaultTextureKey;
  std::string ModelFile;
  float AnimTimeSec{0.f};
  std::string ActiveClipName;
  const GltfAnimationCpu *ActiveAnimation{nullptr};
  std::vector<glm::mat4> BoneMatrices;
};

} // namespace cutum

#endif
