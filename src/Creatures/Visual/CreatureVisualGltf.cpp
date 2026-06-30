#include "Creatures/Visual/CreatureVisualGltf.h"

#include "App/Settings/RenderSettings.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Visual/CreatureTextureStorage.h"
#include "Creatures/Visual/Gltf/CreatureAnimationClipMap.h"
#include "Creatures/Visual/Gltf/CreatureGltfAnimPlayer.h"
#include "Creatures/Visual/Gltf/CreatureGltfBonePalette.h"
#include "Creatures/Visual/Gltf/CreatureGltfCache.h"
#include "Creatures/Visual/Gltf/CreatureGltfModelSpace.h"
#include "Render/Engine/GeometryEngine.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <memory>
#include <unordered_set>

namespace cutum
{

namespace
{

std::unordered_set<std::string> gMissingGltfMeshLogged;
std::unordered_set<std::string> gMissingGltfTextureLogged;

GLuint ResolveGltfSpeciesTexture(const UCreatureTextureStorage &textures,
                                 const std::string &speciesId,
                                 const std::string &stem,
                                 const std::string &defaultStem)
{
  const std::string keys[] = {
      speciesId + "/" + stem,
      speciesId + "/diffuse",
      speciesId + "/" + defaultStem,
      speciesId + "/body",
  };
  for (const std::string &key : keys)
  {
    if (!key.empty() && key.back() != '/')
    {
      if (const GLuint tex = textures.GetTexture(key))
      {
        return tex;
      }
    }
  }
  return 0;
}

} // namespace

std::unique_ptr<ICreatureVisual> CreateCreatureVisualGltf()
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
                          ? "body"
                          : animDef.visual.defaultTextureKey;
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
      if (gMissingGltfMeshLogged.insert(key).second)
      {
        std::cerr << "UCreatureVisualGltf: missing mesh for species="
                  << SpeciesId << " model=" << ModelFile << std::endl;
      }
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
    AnimTimeSec += dt * speed;
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

  glm::mat4 bodyMat = glm::translate(glm::mat4(1.f), BodyOrigin);
  if (ModelFeetOffsetY != 0.f)
  {
    bodyMat =
        glm::translate(bodyMat, glm::vec3(0.f, ModelFeetOffsetY, 0.f));
  }
  bodyMat =
      glm::rotate(bodyMat, glm::radians(BodyYaw), glm::vec3(0.f, 1.f, 0.f));
  bodyMat = bodyMat * GltfEntityConventionMatrix();
  if (ModelScale != 1.f)
  {
    bodyMat = bodyMat * glm::scale(glm::mat4(1.f), glm::vec3(ModelScale));
  }
  bodyMat = bodyMat * RootAnimMatrix;

  for (const GltfPrimitiveCpu &prim : MeshAsset->primitives)
  {
    GLuint tex = ResolveGltfSpeciesTexture(*creatureTextures, SpeciesId,
                                           prim.textureStem, DefaultTextureKey);
    if (tex == 0)
    {
      const std::string key = SpeciesId + "|" + prim.textureStem;
      if (gMissingGltfTextureLogged.insert(key).second)
      {
        std::cerr << "UCreatureVisualGltf: missing texture species="
                  << SpeciesId << " stem=" << prim.textureStem << std::endl;
      }
      continue;
    }
    const glm::mat4 mvp = viewProj * bodyMat;
    if (prim.skinned && !BoneMatrices.empty())
    {
      engine.DrawCreatureSkinnedMesh(mvp, tex, prim, BoneMatrices);
    }
    else
    {
      engine.DrawCreatureGltfMesh(mvp, tex, prim.mesh);
    }
  }
}

} // namespace cutum
