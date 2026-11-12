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
#include "Creatures/Visual/Gltf/GltfTransitionGroundClearance.h"
#include "Creatures/Visual/Gltf/GltfDistancePlayback.h"
#include "Creatures/Diagnostics/CreatureQaFootProbe.h"
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

void UCreatureVisualGltf::EnsureMesh(const CreatureDefinition &animDef)
{
  SpeciesId = animDef.Id;
  ModelFile = animDef.visual.gltf.modelPath.empty()
                  ? "model.gltf"
                  : animDef.visual.gltf.modelPath;
  auto &meshCache=CreatureGltfCache::Instance();
  const uint64_t revision=meshCache.GetRevision();
  if (!MeshRequestSeen || LoadedRevision!=revision || LoadedSpecies!=SpeciesId ||
      LoadedFile!=ModelFile || LoadedDirectory!=animDef.visual.sourceDirectory)
  {
    MeshRequestSeen=true;LoadedRevision=revision;LoadedSpecies=SpeciesId;
    LoadedFile=ModelFile;LoadedDirectory=animDef.visual.sourceDirectory;
    ActiveAnimation=nullptr;HasBonePalette=false;BoneMatrices.reset();SkinScratch={};
    // Keep the clip clock: a content refresh must not restart a live walk.
    MeshAsset = meshCache.Load(SpeciesId, ModelFile,LoadedDirectory);
    if (!MeshAsset)
    {
      const std::string key = SpeciesId + "|" + ModelFile;
      LogCreatureMissingTextureOnce(
          key, "UCreatureVisualGltf: missing mesh for species=" + SpeciesId +
                   " model=" + ModelFile);
    }
  }
}

bool UCreatureVisualGltf::GetConservativeWorldBounds(const UCreature &creature,
    const CreatureDefinition &def,glm::vec3 &low,glm::vec3 &high)
{
  EnsureMesh(def);
  const float radius=MeshAsset?MeshAsset->conservativeRenderRadius*std::abs(def.visual.gltf.modelScale):
      std::numeric_limits<float>::infinity();
  const float offset=def.visual.gltf.modelOffsetY-(MeshAsset?MeshAsset->bindMinY*def.visual.gltf.modelScale:0.f);
  const glm::vec3 origin=creature.GetFeetPosition();
  low=origin+glm::vec3(-radius,offset-radius,-radius);
  // Fade clearance can only lift the surface. Also cover retained takeoff
  // decay after a larger previous mesh/scale was replaced while airborne.
  const float lift=std::max(TransitionLift,std::max(0.f,radius-offset+.00001f));
  high=origin+glm::vec3(radius,offset+radius+lift,radius);
  return true;
}

void UCreatureVisualGltf::UpdatePose(const UCreature &creature,
                                     const CreatureLocomotionFacts &facts,
                                     const CreaturePoseParams & /*pose*/,
                                     const CreatureDefinition &animDef,
                                     float dt)
{
  BodyOrigin = creature.GetFeetPosition();
  BodyYawDeg = ComputeBodyYawDeg(creature, animDef);
  RefreshDefaultTextureKey(animDef);
  EnsureMesh(animDef);
  ModelScale = animDef.visual.gltf.modelScale;
  ModelFeetOffsetY = animDef.visual.gltf.modelOffsetY;
  if (MeshAsset)
  {
    // bindMinY is in unscaled model units; the offset is applied in world
    // blocks, so scale it (kitten model_scale 0.35 used to sink/float).
    ModelFeetOffsetY -= MeshAsset->bindMinY * ModelScale;
  }

  RootAnimMatrix = glm::mat4(1.f);
  ActiveAnimation = nullptr;
  HasBonePalette = false;
  if (MeshAsset)
  {
    const auto clipId =
        ResolveAnimationClipId(facts.state, animDef.visual.Animation.stateMap);
    std::string animName = clipId.value_or("idle");
    const auto clipDefIt = animDef.visual.Animation.clips.find(animName);
    float speed = 1.f;
    float groundDistancePerCycle=0.f;
    bool loop = true;
    if (clipDefIt != animDef.visual.Animation.clips.end())
    {
      speed = clipDefIt->second.speed;
      loop = clipDefIt->second.loop;
      groundDistancePerCycle=clipDefIt->second.groundDistancePerCycle;
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
    const auto findAnimation =
        [&](const std::string &name) -> const GltfAnimationCpu *
    {
      const auto it = MeshAsset->animationIndexByName.find(name);
      return it != MeshAsset->animationIndexByName.end()
                 ? &MeshAsset->animations[it->second]
                 : nullptr;
    };
    ActiveAnimation = findAnimation(animName);
    if(groundDistancePerCycle>0.f && facts.onGround && !facts.inFluid &&
       (facts.state==LocomotionState::Walk || facts.state==LocomotionState::Run ||
        facts.state==LocomotionState::Slither))
      playback=GltfDistancePlayback(facts.horizontalSpeed,
          ActiveAnimation?GltfAnimationDuration(*ActiveAnimation):0.f,groundDistancePerCycle);
    ClipClock.Update(animName, dt, playback,
                     ActiveAnimation ? GltfAnimationDuration(*ActiveAnimation)
                                     : 0.f,
                     loop);
    if (MeshAsset->hasSkin)
    {
      const GltfAnimationCpu *fadeAnimation =
          ClipClock.GetFadeClip().empty()
              ? nullptr
              : findAnimation(ClipClock.GetFadeClip());
      if (!BoneMatrices || BoneMatrices.use_count() > 1)
      {
        BoneMatrices = std::make_shared<std::vector<glm::mat4>>();
      }
      ComputeGltfSkinMatricesInto(
          *MeshAsset, ActiveAnimation, ClipClock.GetTime(), fadeAnimation,
          ClipClock.GetFadeTime(),
          fadeAnimation ? ClipClock.GetFadeWeight() : 0.f, SkinScratch,
          *BoneMatrices);
      HasBonePalette = true;
    }
    else if (ActiveAnimation)
    {
      RootAnimMatrix =
          SampleGltfRootTransform(*ActiveAnimation, ClipClock.GetTime(), loop);
      // Root-only models switch clips with a hard transform snap (the fade
      // below only covered skinned rigs). Blend the leaving clip's root the
      // same 0.15 s the bone palette gets.
      if (!ClipClock.GetFadeClip().empty())
      {
        const GltfAnimationCpu *fadeAnimation =
            findAnimation(ClipClock.GetFadeClip());
        if (fadeAnimation)
        {
          const glm::mat4 leaving = SampleGltfRootTransform(
              *fadeAnimation, ClipClock.GetFadeTime(), loop);
          const float w = ClipClock.GetFadeWeight();
          RootAnimMatrix = leaving * (1.f - w) + RootAnimMatrix * w;
        }
      }
    }
  }
  const bool supported=UsesGltfTransitionGroundClearance(facts,animDef);
  static const std::vector<glm::mat4> emptyPalette;
  const float required=supported && MeshAsset && !ClipClock.GetFadeClip().empty()
      ? ComputeGltfTransitionGroundClearance(*MeshAsset,
          BoneMatrices ? *BoneMatrices : emptyPalette,
          RootAnimMatrix,ModelScale,ModelFeetOffsetY) : 0.f;
  TransitionLift=TransitionGroundClearance.Update(required,supported,dt);
  ModelFeetOffsetY+=TransitionLift;
  RememberCreatureQaFootPose(this,creature,facts,dt,BodyYawDeg,ClipClock);
}

void UCreatureVisualGltf::SubmitDraw(const CreatureDrawContext &ctx,
                                     const glm::mat4 &viewProj)
{
  if (!MeshAsset || !DrawEnabled(ctx))
  {
    return;
  }
  const UCreatureTextureStorage &creatureTextures = *ctx.Textures;

  const glm::mat4 bodyMat = BuildCreatureRootMatrix(
      BodyOrigin, BodyYawDeg, ModelFeetOffsetY, []()
      { return GltfEntityConventionMatrix(); }, ModelScale, RootAnimMatrix);

  CreatureDrawQueue &queue = *ctx.Queue;
  const size_t firstRequest = queue.Size();
  for (const GltfPrimitiveCpu &prim : MeshAsset->primitives)
  {
    const GLuint tex = ResolveCreatureSpeciesTexture(
        creatureTextures, SpeciesId, prim.textureStem, DefaultTextureKey,
        &Appearance);
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
    if (prim.skinned && HasBonePalette && BoneMatrices &&
        !BoneMatrices->empty())
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
  if(!CreatureQaFootProbePath().empty() && queue.Size()>firstRequest && HasBonePalette && BoneMatrices)
    RecordCreatureQaFootDraw(this,MeshAsset,*BoneMatrices,bodyMat);
}

} // namespace cutum
