#include "Creatures/Visual/CreatureVisualSkeletalGeo.h"

#include "Creatures/Core/Creature.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Visual/Skeletal/CreatureSkeletalGeoCache.h"
#include "Creatures/Visual/Skeletal/CreatureBoneHierarchy.h"
#include "Creatures/Visual/Skeletal/SkeletalModelSpace.h"
#include "Creatures/Visual/CreatureTextureStorage.h"
#include "Pose/Skeletal/SkeletalBonePoseEngine.h"
#include "Render/Engine/GeometryEngine.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <unordered_set>

namespace cutum
{

namespace
{

std::unordered_set<std::string> gMissingSkeletalMeshLogged;
std::unordered_set<std::string> gMissingSkeletalTextureLogged;

GLuint ResolveSkeletalSpeciesTexture(const UCreatureTextureStorage &textures,
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

UCreatureVisualSkeletalGeo::~UCreatureVisualSkeletalGeo() = default;

std::unique_ptr<ICreatureVisual> CreateCreatureVisualSkeletalGeo()
{
  return std::make_unique<UCreatureVisualSkeletalGeo>();
}

void UCreatureVisualSkeletalGeo::UpdatePose(const UCreature &creature,
                                          const CreatureLocomotionFacts &facts,
                                          const CreaturePoseParams & /*pose*/,
                                          const CreatureDefinition &animDef,
                                          float dt)
{
  BodyOrigin = creature.GetFeetPosition();
  BodyYaw = creature.GetYaw() + animDef.visual.modelYawOffsetDeg;
  SpeciesId = animDef.Id;
  SkinId = creature.GetSkinId();
  const std::string geoFile = animDef.visual.skeletal.geometryFile.empty()
                                  ? "geometry.geo.json"
                                  : animDef.visual.skeletal.geometryFile;
  GeometryId = animDef.visual.skeletal.geometryId;
  TextureStem = animDef.visual.skeletal.textureStem.empty()
                    ? "diffuse"
                    : animDef.visual.skeletal.textureStem;
  DefaultTextureKey = animDef.visual.defaultTextureKey.empty()
                          ? "body"
                          : animDef.visual.defaultTextureKey;

  if (!MeshAsset || MeshAsset->geometry.identifier != GeometryId)
  {
    MeshAsset = CreatureSkeletalGeoCache::Instance().Load(SpeciesId, geoFile,
                                                       GeometryId);
    if (MeshAsset)
    {
      Hierarchy = std::make_unique<CreatureBoneHierarchy>(MeshAsset->geometry);
    }
    else
    {
      const std::string key = SpeciesId + "|" + geoFile + "|" + GeometryId;
      if (gMissingSkeletalMeshLogged.insert(key).second)
      {
        std::cerr << "UCreatureVisualSkeletalGeo: missing mesh asset for species="
                  << SpeciesId << " geoFile=" << geoFile
                  << " geometryId=" << GeometryId << std::endl;
      }
    }
  }

  BonePose = SkeletalBonePoseEngine::Compute(facts, animDef, dt);
}

void UCreatureVisualSkeletalGeo::SubmitDraw(UGeometryEngine &engine,
                                           const glm::mat4 &viewProj)
{
  if (!MeshAsset || !Hierarchy)
  {
    return;
  }

  const RenderSettings &settings = engine.GetRenderSettings();
  auto creatureTextures = engine.GetCreatureTextureStorage();
  if (!creatureTextures || !settings.CreatureTexturedParts)
  {
    return;
  }

  GLuint tex = ResolveSkeletalSpeciesTexture(*creatureTextures, SpeciesId,
                                            TextureStem, DefaultTextureKey);
  if (tex == 0 && !SkinId.empty())
  {
    tex = creatureTextures->GetTexture("skin/" + SkinId + "/" + TextureStem);
  }
  if (tex == 0 && !SkinId.empty())
  {
    tex = creatureTextures->GetTexture("skin/" + SkinId + "/diffuse");
  }
  if (tex == 0)
  {
    const std::string key = SpeciesId + "|" + TextureStem + "|" + DefaultTextureKey;
    if (gMissingSkeletalTextureLogged.insert(key).second)
    {
      std::cerr << "UCreatureVisualSkeletalGeo: missing texture for species="
                << SpeciesId << " stem=" << TextureStem
                << " default=" << DefaultTextureKey << " skin=" << SkinId
                << std::endl;
    }
    return;
  }

  glm::mat4 bodyMat = glm::translate(glm::mat4(1.f), BodyOrigin);
  bodyMat =
      glm::rotate(bodyMat, glm::radians(BodyYaw), glm::vec3(0.f, 1.f, 0.f));
  bodyMat = bodyMat * SkeletalEntityConventionMatrix();

  for (size_t boneIdx = 0; boneIdx < MeshAsset->boneMeshes.size(); ++boneIdx)
  {
    const glm::mat4 boneMat =
        bodyMat * Hierarchy->ComputeBoneMatrix(boneIdx, BonePose);
    const SkeletalBoneMeshCpu &boneMesh = MeshAsset->boneMeshes[boneIdx];
    for (const SkeletalCubeMeshCpu &cube : boneMesh.cubes)
    {
      if (cube.interleavedPosUv.empty() || cube.indices.empty())
      {
        continue;
      }
      const glm::mat4 mvp = viewProj * boneMat * cube.restLocalMatrix;
      engine.DrawCreatureSkeletalMesh(mvp, tex, cube);
    }
  }

  if (settings.CreatureWireframeOverlay)
  {
    for (size_t boneIdx = 0; boneIdx < MeshAsset->boneMeshes.size(); ++boneIdx)
    {
      const glm::mat4 boneMat =
          bodyMat * Hierarchy->ComputeBoneMatrix(boneIdx, BonePose);
      const SkeletalBoneMeshCpu &boneMesh = MeshAsset->boneMeshes[boneIdx];
      for (const SkeletalCubeMeshCpu &cube : boneMesh.cubes)
      {
        if (cube.interleavedPosUv.empty())
        {
          continue;
        }
        engine.DrawBoxWireframe(viewProj * boneMat * cube.restLocalMatrix,
                                Appearance.wireframeColor);
      }
    }
  }
}

} // namespace cutum
