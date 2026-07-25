#include "Creatures/Visual/CreatureVisualGltf.h"

#include "App/Settings/RenderSettings.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Visual/CreatureDrawRequest.h"
#include "Creatures/Visual/CreatureRootTransform.h"
#include "Creatures/Visual/CreatureTextureResolver.h"
#include "Creatures/Visual/CreatureTextureStorage.h"
#include "Creatures/Visual/Gltf/CreatureAnimationClipMap.h"
#include "Creatures/Visual/Gltf/CreatureGltfAnimPlayer.h"
#include "Creatures/Visual/Gltf/CreatureGltfBonePalette.h"
#include "Creatures/Visual/Gltf/CreatureGltfCache.h"
#include "Creatures/Visual/Gltf/CreatureGltfModelSpace.h"
#include "Render/Engine/GeometryEngine.h"
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <memory>

namespace cutum
{

std::unique_ptr<IUCreatureVisual> CreateCreatureVisualGltf()
{
  return std::make_unique<UCreatureVisualGltf>();
}

void UCreatureVisualGltf::UpdatePose(const UCreature &creature,
                                     const CreatureLocomotionFacts &facts,
                                     const CreaturePoseParams & /*pose*/,
                                     const CreatureDefinition &animDef,
                                     float dt)
{
  BodyOrigin = creature.GetFeetPosition();
  BodyYaw = creature.GetYaw() + animDef.visual.modelYawOffsetDeg;
  SpeciesId = animDef.Id;
  DefaultTextureKey = animDef.visual.defaultTextureKey.empty()
                          ? Appearance.defaultTextureKey
                          : animDef.visual.defaultTextureKey;
  if (DefaultTextureKey.empty())
  {
    DefaultTextureKey = "body";
  }
  ModelFile = animDef.visual.gltf.modelPath.empty()
                  ? "model.gltf"
                  : animDef.visual.gltf.modelPath;
  ModelScale = animDef.visual.gltf.modelScale;
  ModelFeetOffsetY = animDef.visual.gltf.modelOffsetY;

  if (!MeshAsset)
  {
    MeshAsset = CreatureGltfCache::Instance().Load(SpeciesId, ModelFile);
    if (!MeshAsset)
    {
      const std::string key = SpeciesId + "|" + ModelFile;
      LogCreatureMissingTextureOnce(
          key, "UCreatureVisualGltf: missing mesh for species=" + SpeciesId +
                   " model=" + ModelFile);
    }
  }
  if (MeshAsset)
  {
    ModelFeetOffsetY -= MeshAsset->bindMinY;
  }

  RootAnimMatrix = glm::mat4(1.f);
  ActiveAnimation = nullptr;
  BoneMatrices.clear();
  if (MeshAsset)
  {
    const auto clipId =
        ResolveAnimationClipId(facts.state, animDef.visual.Animation.stateMap);
    std::string animName = clipId.value_or("idle");
    const auto clipDefIt = animDef.visual.Animation.clips.find(animName);
    float speed = 1.f;
    bool loop = true;
    if (clipDefIt != animDef.visual.Animation.clips.end())
    {
      speed = clipDefIt->second.speed;
      loop = clipDefIt->second.loop;
    }
    if (animName != ActiveClipName)
    {
      ActiveClipName = animName;
      AnimTimeSec = 0.f;
    }
    float playback = speed;
    if (facts.state == LocomotionState::Walk ||
        facts.state == LocomotionState::Run ||
        facts.state == LocomotionState::Slither ||
        facts.state == LocomotionState::Swim)
    {
      // Full-rate walk while scraping looks like L/R rocking around the body
      // (treeman/dmobs). Gate clip speed by actual travel vs walk_speed.
      const float walkRef = std::max(animDef.locomotion.walkSpeed, 0.05f);
      float travelScale =
          std::clamp(facts.horizontalSpeed / walkRef, 0.0f, 1.5f);
      if (travelScale < 0.04f)
      {
        travelScale = 0.0f;
      }
      else
      {
        travelScale = std::max(0.22f, travelScale);
      }
      playback *= travelScale;
    }
    AnimTimeSec += dt * playback;
    const auto animIt = MeshAsset->animationIndexByName.find(animName);
    if (animIt != MeshAsset->animationIndexByName.end())
    {
      ActiveAnimation = &MeshAsset->animations[animIt->second];
      if (MeshAsset->hasSkin)
      {
        BoneMatrices = ComputeGltfSkinMatrices(*MeshAsset, ActiveAnimation,
                                               AnimTimeSec, loop);
      }
      else
      {
        RootAnimMatrix =
            SampleGltfRootTransform(*ActiveAnimation, AnimTimeSec, loop);
      }
    }
    else if (MeshAsset->hasSkin)
    {
      BoneMatrices =
          ComputeGltfSkinMatrices(*MeshAsset, nullptr, AnimTimeSec, loop);
    }
  }
}

void UCreatureVisualGltf::SubmitDraw(UGeometryEngine &engine,
                                     const glm::mat4 &viewProj)
{
  if (!MeshAsset)
  {
    return;
  }

  const RenderSettings &settings = engine.GetRenderSettings();
  auto creatureTextures = engine.GetCreatureTextureStorage();
  if (!creatureTextures || !settings.CreatureTexturedParts)
  {
    return;
  }

  const glm::mat4 bodyMat = BuildCreatureRootMatrix(
      BodyOrigin, BodyYaw, ModelFeetOffsetY, []()
      { return GltfEntityConventionMatrix(); }, ModelScale, RootAnimMatrix);

  CreatureDrawQueue &queue = engine.GetCreatureDrawQueue();
  for (const GltfPrimitiveCpu &prim : MeshAsset->primitives)
  {
    const GLuint tex = ResolveCreatureSpeciesTexture(
        *creatureTextures, SpeciesId, prim.textureStem, DefaultTextureKey);
    if (tex == 0)
    {
      const std::string key = SpeciesId + "|" + prim.textureStem;
      LogCreatureMissingTextureOnce(
          key, "UCreatureVisualGltf: missing texture species=" + SpeciesId +
                   " stem=" + prim.textureStem);
      continue;
    }
    const glm::mat4 mvp = viewProj * bodyMat;
    CreatureDrawRequest req;
    req.Mvp = mvp;
    req.Texture = tex;
    if (prim.skinned && !BoneMatrices.empty())
    {
      req.Kind = CreatureDrawKind::SkinnedMesh;
      req.SkinnedPrimitive = &prim;
      req.BoneMatrices = BoneMatrices;
    }
    else
    {
      req.Kind = CreatureDrawKind::SkeletalMesh;
      req.SkeletalMesh = &prim.mesh;
    }
    queue.Push(std::move(req));
  }
}

} // namespace cutum
