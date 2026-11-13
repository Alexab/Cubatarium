#include "Creatures/Visual/CreatureVisualBoneSkeleton.h"

#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureInventory.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Visual/CreatureDrawRequest.h"
#include "Creatures/Visual/CreatureRootTransform.h"
#include "Creatures/Visual/CreatureTextureResolver.h"
#include "Creatures/Visual/CreatureTextureStorage.h"
#include "Creatures/Visual/WornEquipmentDrawer.h"
#include "Items/ItemDefinitionStorage.h"
#include "Creatures/Visual/BoneSkeleton/BoneSkeletonHierarchy.h"
#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonCache.h"
#include "Creatures/Visual/BoneSkeleton/BoneSkeletonModelSpace.h"
#include "Game/Inventory/InventoryTypes.h"
#include "Pose/BoneSkeleton/BoneSkeletonPoseEngine.h"
#include "Pose/BoneSkeleton/SkeletalAnimationProfiles.h"
#include "Render/Engine/GeometryEngine.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <limits>

namespace cutum
{

UCreatureVisualBoneSkeleton::~UCreatureVisualBoneSkeleton() = default;

bool UCreatureVisualBoneSkeleton::GetWorldBoneTransform(
    const std::string &name, glm::mat4 &out) const
{
  if (!MeshAsset) return false;
  const auto bone = MeshAsset->geometry.boneIndexByName.find(name);
  if (bone == MeshAsset->geometry.boneIndexByName.end() ||
      bone->second >= CachedBoneMatrices.size()) return false;
  const glm::mat4 root = BuildCreatureRootMatrix(BodyOrigin, BodyYawDeg, 0.f,
      []() { return BoneSkeletonEntityConventionMatrix(); });
  out = root * CachedBoneMatrices[bone->second];
  return true;
}

std::unique_ptr<IUCreatureVisual> CreateCreatureVisualBoneSkeleton()
{
  return std::make_unique<UCreatureVisualBoneSkeleton>();
}

bool UCreatureVisualBoneSkeleton::GetBoneLocalBounds(
    const std::string &name, glm::vec3 &low, glm::vec3 &high) const
{
  if (!MeshAsset || !Hierarchy) return false;
  if (const auto cached=AttachmentBounds.find(name);cached!=AttachmentBounds.end())
  { low=cached->second.first;high=cached->second.second;return true; }
  const auto anchor=MeshAsset->geometry.boneIndexByName.find(name);
  if (anchor==MeshAsset->geometry.boneIndexByName.end()) return false;
  size_t source=anchor->second;
  while (MeshAsset->boneMeshes[source].cubes.empty())
  {
    const auto parent=MeshAsset->geometry.boneIndexByName.find(MeshAsset->geometry.bones[source].parent);
    if (parent==MeshAsset->geometry.boneIndexByName.end()) return false;
    source=parent->second;
  }
  const BoneSkeletonPose rest;
  const glm::mat4 intoAnchor=glm::inverse(Hierarchy->ComputeBoneMatrix(anchor->second,rest)) *
      Hierarchy->ComputeBoneMatrix(source,rest);
  low=glm::vec3(std::numeric_limits<float>::max());high=-low;bool any=false;
  for (const auto &cube:MeshAsset->boneMeshes[source].cubes)
  {
    const auto matrix=intoAnchor*cube.restLocalMatrix;const auto &v=cube.interleavedPosUv;
    for(size_t i=0;i+4<v.size();i+=5)
    { const glm::vec3 point(matrix*glm::vec4(v[i],v[i+1],v[i+2],1.f));low=glm::min(low,point);high=glm::max(high,point);any=true; }
  }
  if(any) AttachmentBounds.emplace(name,std::make_pair(low,high));
  return any;
}

void UCreatureVisualBoneSkeleton::UpdatePose(
    const UCreature &creature, const CreatureLocomotionFacts &facts,
    const CreaturePoseParams & /*pose*/, const CreatureDefinition &animDef,
    float dt)
{
  BodyOrigin = creature.GetFeetPosition();
  BodyYawDeg = ComputeBodyYawDeg(creature, animDef);
  SpeciesId = animDef.Id;
  const std::string geoFile = !Appearance.geometryFileOverride.empty() ? Appearance.geometryFileOverride : animDef.visual.boneSkeleton.geometryFile.empty()
                                  ? "geometry.geo.json"
                                  : animDef.visual.boneSkeleton.geometryFile;
  GeometryId = animDef.visual.boneSkeleton.geometryId;
  TextureStem = animDef.visual.boneSkeleton.textureStem.empty()
                    ? "diffuse"
                    : animDef.visual.boneSkeleton.textureStem;
  RefreshDefaultTextureKey(animDef);

  auto &meshCache=CreatureBoneSkeletonCache::Instance();
  const uint64_t revision=meshCache.GetRevision();
  if (!MeshRequestSeen || LoadedRevision!=revision || LoadedSpecies!=SpeciesId ||
      LoadedFile!=geoFile || LoadedGeometry!=GeometryId ||
      LoadedDirectory!=animDef.visual.sourceDirectory)
  {
    MeshRequestSeen=true;LoadedRevision=revision;LoadedSpecies=SpeciesId;
    LoadedFile=geoFile;LoadedGeometry=GeometryId;LoadedDirectory=animDef.visual.sourceDirectory;
    Hierarchy.reset();AttachmentBounds.clear();CachedBoneMatrices.clear();
    GroundClearance={};
    MeshAsset = meshCache.Load(SpeciesId, geoFile,GeometryId,LoadedDirectory);
    if (MeshAsset)
    {
      Hierarchy = std::make_unique<BoneSkeletonHierarchy>(MeshAsset->geometry);
    }
    else
    {
      const std::string key = SpeciesId + "|" + geoFile + "|" + GeometryId;
      LogCreatureMissingTextureOnce(
          key, "UCreatureVisualBoneSkeleton: missing mesh asset for species=" +
                   SpeciesId + " geoFile=" + geoFile +
                   " geometryId=" + GeometryId);
    }
  }

  BonePose = BoneSkeletonPoseEngine::Compute(facts, animDef, dt);
  CachedBoneMatrices.clear();
  if (MeshAsset && Hierarchy)
  {
    CachedBoneMatrices.resize(MeshAsset->boneMeshes.size());
    for (size_t boneIdx = 0; boneIdx < CachedBoneMatrices.size(); ++boneIdx)
    {
      CachedBoneMatrices[boneIdx] =
          Hierarchy->ComputeBoneMatrix(boneIdx, BonePose);
    }
  }

  const bool groundClearance = UsesBoneGroundClearance(facts, animDef,
      SkeletalAnimationProfiles::ResolveProfileId(animDef));
  const float requiredLift = groundClearance && MeshAsset
      ? ComputeBoneGroundClearance(*MeshAsset, CachedBoneMatrices) : 0.f;
  // The common root also carries equipment. Bone matrices, pivots, authored
  // rotations and physics/collider position remain untouched. On takeoff the
  // small clearance offset decays instead of snapping off in one frame.
  BodyOrigin.y += GroundClearance.Update(requiredLift, groundClearance, dt);

  PoseCreature = &creature;
}

void UCreatureVisualBoneSkeleton::SubmitDraw(const CreatureDrawContext &ctx,
                                            const glm::mat4 &viewProj)
{
  if (!MeshAsset || !Hierarchy || CachedBoneMatrices.empty() ||
      !DrawEnabled(ctx))
  {
    return;
  }
  const UCreatureTextureStorage &creatureTextures = *ctx.Textures;
  const RenderSettings &settings = *ctx.Settings;

  const GLuint tex = ResolveCreatureSpeciesTexture(
      creatureTextures, SpeciesId, TextureStem, DefaultTextureKey, &Appearance);
  if (tex == 0)
  {
    const std::string key =
        SpeciesId + "|" + TextureStem + "|" + DefaultTextureKey;
    LogCreatureMissingTextureOnce(
        key, "UCreatureVisualBoneSkeleton: missing texture for species=" +
                 SpeciesId + " stem=" + TextureStem +
                 " default=" + DefaultTextureKey + " skin=" + Appearance.effectiveSkinId);
    return;
  }

  const glm::mat4 bodyMat =
      BuildCreatureRootMatrix(BodyOrigin, BodyYawDeg, 0.f, []()
                              { return BoneSkeletonEntityConventionMatrix(); });

  CreatureDrawQueue &queue = *ctx.Queue;
  for (size_t boneIdx = 0; boneIdx < MeshAsset->boneMeshes.size(); ++boneIdx)
  {
    const auto &headGear = PoseCreature->GetInventory().GetEquippedArmor(0);
    if (MeshAsset->geometry.bones[boneIdx].name == "hair" &&
        (Appearance.hideHair || (!headGear.empty && !headGear.broken && !headGear.Id.empty() && headGear.wear < 1.f))) continue;
    const glm::mat4 boneMat = bodyMat * CachedBoneMatrices[boneIdx];
    const BoneSkeletonBoneMeshCpu &boneMesh = MeshAsset->boneMeshes[boneIdx];
    for (const BoneSkeletonCubeMeshCpu &cube : boneMesh.cubes)
    {
      if (cube.interleavedPosUv.empty() || cube.indices.empty())
      {
        continue;
      }
      CreatureDrawRequest req;
      req.Kind = CreatureDrawKind::SkeletalMesh;
      req.Mvp = viewProj * boneMat * cube.restLocalMatrix;
      req.Texture = tex;
      req.SkeletalMesh = &cube;
      queue.Push(std::move(req));
    }
  }

  if (settings.CreatureWireframeOverlay)
  {
    for (size_t boneIdx = 0; boneIdx < MeshAsset->boneMeshes.size(); ++boneIdx)
    {
      const auto &headGear = PoseCreature->GetInventory().GetEquippedArmor(0);
    if (MeshAsset->geometry.bones[boneIdx].name == "hair" &&
        (Appearance.hideHair || (!headGear.empty && !headGear.broken && !headGear.Id.empty() && headGear.wear < 1.f))) continue;
    const glm::mat4 boneMat = bodyMat * CachedBoneMatrices[boneIdx];
      const BoneSkeletonBoneMeshCpu &boneMesh = MeshAsset->boneMeshes[boneIdx];
      for (const BoneSkeletonCubeMeshCpu &cube : boneMesh.cubes)
      {
        if (cube.interleavedPosUv.empty())
        {
          continue;
        }
        CreatureDrawRequest req;
        req.Kind = CreatureDrawKind::WireframeBox;
        req.Mvp = viewProj * boneMat * cube.restLocalMatrix;
        req.WireColor = Appearance.wireframeColor;
        queue.Push(std::move(req));
      }
    }
  }

  if (PoseCreature)
  {
    const auto boneLookup =
        [this](const std::string &boneName, glm::mat4 &out) -> bool
        {
          if (!MeshAsset || !Hierarchy)
          {
            return false;
          }
          const auto it = MeshAsset->geometry.boneIndexByName.find(boneName);
          if (it == MeshAsset->geometry.boneIndexByName.end())
          {
            return false;
          }
          if (it->second >= CachedBoneMatrices.size())
          {
            return false;
          }
          out = CachedBoneMatrices[it->second];
          return true;
        };
    // Worn equipment submits through the same draw queue as the body; item
    // definitions come with the context (scale falls back to category
    // defaults via id when the storage is absent).
    if (ctx.Queue)
    {
      const BoneBoundsLookupFn bounds=[this](const std::string &name,glm::vec3 &low,glm::vec3 &high)
          {return GetBoneLocalBounds(name,low,high);};
      WornEquipmentDrawer::SubmitFromCreature(*ctx.Queue, *PoseCreature,
                                              viewProj, bodyMat, boneLookup, bounds);
      WornEquipmentDrawer::SubmitWieldedFromCreature(
          *ctx.Queue, *PoseCreature, ctx.Items, viewProj, bodyMat, boneLookup, bounds);
    }
  }
}

} // namespace cutum
