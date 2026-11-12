#ifndef CREATURE_VISUAL_BONE_SKELETON_H
#define CREATURE_VISUAL_BONE_SKELETON_H

#include "Creatures/Visual/CreatureVisualBase.h"
#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonTypes.h"
#include "Creatures/Visual/BoneSkeleton/BoneSkeletonGroundClearance.h"
#include <array>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace cutum
{

class BoneSkeletonHierarchy;

class UCreatureVisualBoneSkeleton : public UCreatureVisualBase
{
public:
  ~UCreatureVisualBoneSkeleton() override;

  void UpdatePose(const UCreature &creature,
                  const CreatureLocomotionFacts &facts,
                  const CreaturePoseParams & /*pose*/,
                  const CreatureDefinition &animDef, float dt) override;
  void SubmitDraw(const CreatureDrawContext &ctx, const glm::mat4 &viewProj) override;
  bool GetWorldBoneTransform(const std::string &name, glm::mat4 &out) const override;
  bool GetBoneLocalBounds(const std::string &name, glm::vec3 &low, glm::vec3 &high) const override;

private:
  std::string GeometryId;
  std::string TextureStem{"diffuse"};
  std::string LoadedSpecies,LoadedFile,LoadedGeometry,LoadedDirectory;
  uint64_t LoadedRevision{0};
  bool MeshRequestSeen{false};
  BoneSkeletonPose BonePose;
  std::vector<glm::mat4> CachedBoneMatrices;
  std::shared_ptr<const CreatureBoneSkeletonMeshAsset> MeshAsset;
  std::unique_ptr<BoneSkeletonHierarchy> Hierarchy;
  BoneGroundClearanceState GroundClearance;
  mutable std::unordered_map<std::string, std::pair<glm::vec3,glm::vec3>> AttachmentBounds;
  const UCreature *PoseCreature{nullptr};
};

std::unique_ptr<IUCreatureVisual> CreateCreatureVisualBoneSkeleton();

} // namespace cutum

#endif
