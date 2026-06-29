#ifndef CREATURE_VISUAL_SKELETAL_GEO_H
#define CREATURE_VISUAL_SKELETAL_GEO_H

#include "Creatures/Visual/CreatureVisual.h"
#include "Creatures/Visual/Skeletal/CreatureSkeletalTypes.h"
#include <memory>
#include <string>

namespace cutum
{

class CreatureBoneHierarchy;

class UCreatureVisualSkeletalGeo : public ICreatureVisual
{
public:
  ~UCreatureVisualSkeletalGeo() override;

  void UpdatePose(const UCreature &creature,
                  const CreatureLocomotionFacts &facts,
                  const CreaturePoseParams & /*pose*/,
                  const CreatureDefinition &animDef, float dt) override;
  void SubmitDraw(UGeometryEngine &engine,
                  const glm::mat4 &viewProj) override;

private:
  glm::vec3 BodyOrigin{0.f};
  float BodyYaw{0.f};
  std::string SpeciesId;
  std::string SkinId;
  std::string GeometryId;
  std::string TextureStem{"diffuse"};
  std::string DefaultTextureKey{"body"};
  SkeletalCreaturePose BonePose;
  std::shared_ptr<const CreatureSkeletalMeshAsset> MeshAsset;
  std::unique_ptr<CreatureBoneHierarchy> Hierarchy;
};

std::unique_ptr<ICreatureVisual> CreateCreatureVisualSkeletalGeo();

} // namespace cutum

#endif
