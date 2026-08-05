#ifndef CREATURE_VISUAL_BONE_SKELETON_H
#define CREATURE_VISUAL_BONE_SKELETON_H

#include "Creatures/Visual/CreatureVisual.h"
#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonTypes.h"
#include <array>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace cutum
{

class BoneSkeletonHierarchy;

class UCreatureVisualBoneSkeleton : public IUCreatureVisual
{
public:
  ~UCreatureVisualBoneSkeleton() override;

  void UpdatePose(const UCreature &creature,
                  const CreatureLocomotionFacts &facts,
                  const CreaturePoseParams & /*pose*/,
                  const CreatureDefinition &animDef, float dt) override;
  void SubmitDraw(UGeometryEngine &engine, const glm::mat4 &viewProj) override;

private:
  glm::vec3 BodyOrigin{0.f};
  float BodyYaw{0.f};
  std::string SpeciesId;
  std::string SkinId;
  std::string GeometryId;
  std::string TextureStem{"diffuse"};
  std::string DefaultTextureKey{"body"};
  BoneSkeletonPose BonePose;
  std::vector<glm::mat4> CachedBoneMatrices;
  std::shared_ptr<const CreatureBoneSkeletonMeshAsset> MeshAsset;
  std::unique_ptr<BoneSkeletonHierarchy> Hierarchy;
  /// Equipped armor item ids snapshot (empty = none), size 6.
  std::array<std::string, 6> EquippedArmorIds{};
  const UCreature *PoseCreature{nullptr};
};

std::unique_ptr<IUCreatureVisual> CreateCreatureVisualBoneSkeleton();

} // namespace cutum

#endif
