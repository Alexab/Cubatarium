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
#include "Render/Engine/GeometryEngine.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

namespace cutum
{

UCreatureVisualBoneSkeleton::~UCreatureVisualBoneSkeleton() = default;

std::unique_ptr<IUCreatureVisual> CreateCreatureVisualBoneSkeleton()
{
  return std::make_unique<UCreatureVisualBoneSkeleton>();
}

void UCreatureVisualBoneSkeleton::UpdatePose(
    const UCreature &creature, const CreatureLocomotionFacts &facts,
    const CreaturePoseParams & /*pose*/, const CreatureDefinition &animDef,
    float dt)
{
  BodyOrigin = creature.GetFeetPosition();
  BodyYawDeg = ComputeBodyYawDeg(creature, animDef);
  SpeciesId = animDef.Id;
  SkinId = creature.GetSkinId();
  const std::string geoFile = animDef.visual.boneSkeleton.geometryFile.empty()
                                  ? "geometry.geo.json"
                                  : animDef.visual.boneSkeleton.geometryFile;
  GeometryId = animDef.visual.boneSkeleton.geometryId;
  TextureStem = animDef.visual.boneSkeleton.textureStem.empty()
                    ? "diffuse"
                    : animDef.visual.boneSkeleton.textureStem;
  RefreshDefaultTextureKey(animDef);

  if (!MeshAsset || MeshAsset->geometry.identifier != GeometryId)
  {
    MeshAsset = CreatureBoneSkeletonCache::Instance().Load(SpeciesId, geoFile,
                                                          GeometryId);
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

  PoseCreature = &creature;
  for (size_t i = 0; i < EquippedArmorIds.size(); ++i)
  {
    EquippedArmorIds[i].clear();
    const InventoryEntryRef &e = creature.GetInventory().GetEquippedArmor(i);
    if (!e.empty && !e.broken && e.kind == InventoryEntryKind::Item)
    {
      EquippedArmorIds[i] = e.Id;
    }
  }
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
      creatureTextures, SpeciesId, TextureStem, DefaultTextureKey, SkinId);
  if (tex == 0)
  {
    const std::string key =
        SpeciesId + "|" + TextureStem + "|" + DefaultTextureKey;
    LogCreatureMissingTextureOnce(
        key, "UCreatureVisualBoneSkeleton: missing texture for species=" +
                 SpeciesId + " stem=" + TextureStem +
                 " default=" + DefaultTextureKey + " skin=" + SkinId);
    return;
  }

  const glm::mat4 bodyMat =
      BuildCreatureRootMatrix(BodyOrigin, BodyYawDeg, 0.f, []()
                              { return BoneSkeletonEntityConventionMatrix(); });

  CreatureDrawQueue &queue = *ctx.Queue;
  for (size_t boneIdx = 0; boneIdx < MeshAsset->boneMeshes.size(); ++boneIdx)
  {
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
    // Transitional: the worn-equipment drawer still submits through
    // engine-level helpers shared with the GUI preview (audit V13 note).
    if (ctx.Engine)
    {
      WornEquipmentDrawer::SubmitFromCreature(*ctx.Engine, *PoseCreature,
                                              viewProj, bodyMat, boneLookup);
      const UItemDefinitionStorage *items = nullptr;
      // Items storage is optional; scale falls back to category defaults via id.
      WornEquipmentDrawer::SubmitWieldedFromCreature(
          *ctx.Engine, *PoseCreature, items, viewProj, bodyMat, boneLookup);
    }
  }
}

} // namespace cutum
