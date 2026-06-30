#include "Creatures/Visual/CreatureVisualBoneSkeleton.h"

#include "Creatures/Core/Creature.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Visual/CreatureDrawRequest.h"
#include "Creatures/Visual/CreatureRootTransform.h"
#include "Creatures/Visual/CreatureTextureResolver.h"
#include "Creatures/Visual/CreatureTextureStorage.h"
#include "Creatures/Visual/BoneSkeleton/BoneSkeletonHierarchy.h"
#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonCache.h"
#include "Creatures/Visual/BoneSkeleton/BoneSkeletonModelSpace.h"
#include "Pose/BoneSkeleton/BoneSkeletonPoseEngine.h"
#include "Render/Engine/GeometryEngine.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

namespace cutum
{

UCreatureVisualBoneSkeleton::~UCreatureVisualBoneSkeleton() = default;

std::unique_ptr<ICreatureVisual> CreateCreatureVisualBoneSkeleton()
{
  return std::make_unique<UCreatureVisualBoneSkeleton>();
}

void UCreatureVisualBoneSkeleton::UpdatePose(
    const UCreature &creature, const CreatureLocomotionFacts &facts,
    const CreaturePoseParams & /*pose*/, const CreatureDefinition &animDef,
    float dt)
{
  BodyOrigin = creature.GetFeetPosition();
  BodyYaw = creature.GetYaw() + animDef.visual.modelYawOffsetDeg;
  SpeciesId = animDef.Id;
  SkinId = creature.GetSkinId();
  const std::string geoFile = animDef.visual.boneSkeleton.geometryFile.empty()
                                  ? "geometry.geo.json"
                                  : animDef.visual.boneSkeleton.geometryFile;
  GeometryId = animDef.visual.boneSkeleton.geometryId;
  TextureStem = animDef.visual.boneSkeleton.textureStem.empty()
                    ? "diffuse"
                    : animDef.visual.boneSkeleton.textureStem;
  DefaultTextureKey = animDef.visual.defaultTextureKey.empty()
                          ? Appearance.defaultTextureKey
                          : animDef.visual.defaultTextureKey;
  if (DefaultTextureKey.empty())
  {
    DefaultTextureKey = "body";
  }

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
}

void UCreatureVisualBoneSkeleton::SubmitDraw(UGeometryEngine &engine,
                                            const glm::mat4 &viewProj)
{
  if (!MeshAsset || !Hierarchy || CachedBoneMatrices.empty())
  {
    return;
  }

  const RenderSettings &settings = engine.GetRenderSettings();
  auto creatureTextures = engine.GetCreatureTextureStorage();
  if (!creatureTextures || !settings.CreatureTexturedParts)
  {
    return;
  }

  const GLuint tex = ResolveCreatureSpeciesTexture(
      *creatureTextures, SpeciesId, TextureStem, DefaultTextureKey, SkinId);
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
      BuildCreatureRootMatrix(BodyOrigin, BodyYaw, 0.f, []()
                              { return BoneSkeletonEntityConventionMatrix(); });

  CreatureDrawQueue &queue = engine.GetCreatureDrawQueue();
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
}

} // namespace cutum
