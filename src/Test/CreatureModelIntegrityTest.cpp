#include "Creatures/Core/CreatureCatalogTypes.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Visual/CreatureVisualBackendSelect.h"
#include "Creatures/Visual/BoneSkeleton/BoneSkeletonCubeMeshBuilder.h"
#include "Creatures/Visual/BoneSkeleton/BoneSkeletonModelSpace.h"
#include "Creatures/Visual/BoneSkeleton/BoneSkeletonHierarchy.h"
#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonLoader.h"
#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonTypes.h"
#include "Creatures/Visual/Gltf/CreatureGltfLoader.h"
#include "Creatures/Visual/Gltf/CreatureGltfTypes.h"
#include "Pose/BoneSkeleton/BoneSkeletonPoseEngine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{

namespace fs = std::filesystem;

struct Findings
{
  int hardFailures = 0;
  int warnings = 0;
  int speciesChecked = 0;

  void Hard(const std::string &speciesId, const std::string &message)
  {
    std::cerr << "HARD [" << speciesId << "] " << message << '\n';
    ++hardFailures;
  }
  void Warn(const std::string &speciesId, const std::string &message)
  {
    std::cerr << "WARN [" << speciesId << "] " << message << '\n';
    ++warnings;
  }
};

Findings F;

std::string VecStr(const glm::vec3 &v)
{
  return "(" + std::to_string(v.x).substr(0, 5) + "," +
         std::to_string(v.y).substr(0, 5) + "," +
         std::to_string(v.z).substr(0, 5) + ")";
}

void TransformAabb(const glm::mat4 &m, float half, glm::vec3 &outMin,
                   glm::vec3 &outMax)
{
  for (int i = 0; i < 8; ++i)
  {
    const glm::vec4 corner = m * glm::vec4(half * ((i & 1) ? 1.f : -1.f),
                                           half * ((i & 2) ? 1.f : -1.f),
                                           half * ((i & 4) ? 1.f : -1.f),
                                           1.f);
    outMin = glm::min(outMin, glm::vec3(corner));
    outMax = glm::max(outMax, glm::vec3(corner));
  }
}

bool TextureStemExists(const fs::path &texturesDir, const std::string &stem)
{
  if (stem.empty())
  {
    return false;
  }
  return fs::exists(texturesDir / (stem + ".png"));
}

void CheckBoneSkeleton(const std::string &id, const fs::path &speciesDir,
                       const cutum::CreatureDefinition &def, float &outMaxY)
{
  const std::string geoFile = def.visual.boneSkeleton.geometryFile.empty()
                                  ? "geometry.geo.json"
                                  : def.visual.boneSkeleton.geometryFile;
  const fs::path geoPath = speciesDir / geoFile;
  if (!fs::exists(geoPath))
  {
    F.Hard(id, "bone_skeleton geometry file missing: " + geoFile);
    return;
  }
  const auto geometryOpt = cutum::CreatureBoneSkeletonLoader::LoadFromFile(
      geoPath.string(), def.visual.boneSkeleton.geometryId);
  if (!geometryOpt)
  {
    F.Hard(id, "bone_skeleton geometry failed to parse: " + geoFile);
    return;
  }
  const cutum::CreatureBoneSkeletonGeometry &geometry = *geometryOpt;

  // Texture: the stem used at runtime must exist on disk.
  const std::string texStem =
      def.visual.boneSkeleton.textureStem.empty()
          ? "diffuse"
          : def.visual.boneSkeleton.textureStem;
  const fs::path texturesDir = speciesDir / "textures";
  if (!TextureStemExists(texturesDir, texStem) &&
      !TextureStemExists(texturesDir, def.visual.defaultTextureKey))
  {
    F.Hard(id, "bone_skeleton texture missing: stem=" + texStem);
  }

  // UV: bedrock box-unfold rects of the VISIBLE faces must intersect the
  // texture (a fully-outside visible face samples clamped edge texels).
  for (const cutum::BoneSkeletonBoneDef &bone : geometry.bones)
  {
    for (const cutum::BoneSkeletonCubeDef &cube : bone.cubes)
    {
      // Cube dims in texels (bedrock box UV: geo units == texels).
      const int w = std::max(1, static_cast<int>(std::lround(
                                   cube.sizeBlocks.x *
                                   cutum::kBoneSkeletonUnitsPerBlock)));
      const int h = std::max(1, static_cast<int>(std::lround(
                                   cube.sizeBlocks.y *
                                   cutum::kBoneSkeletonUnitsPerBlock)));
      const int d = std::max(1, static_cast<int>(std::lround(
                                   cube.sizeBlocks.z *
                                   cutum::kBoneSkeletonUnitsPerBlock)));
      const int u = cube.uvPixels.x;
      const int v = cube.uvPixels.y;
      struct FaceRect
      {
        int index;
        int x0, y0, x1, y1;
      };
      const FaceRect rects[] = {
          {0, u + d + w + d, v + d, u + d + w + d + w, v + d + h},
          {1, u + d + w, v + d, u + d + w + d, v + d + h},
          {2, u + d, v + d, u + d + w, v + d + h},
          {3, u, v + d, u + d, v + d + h},
          {4, u + d, v, u + d + w, v + d},
          {5, u + d + w, v, u + d + w + w, v + d},
      };
      for (const FaceRect &rect : rects)
      {
        if (!cutum::BoneSkeletonCubeFaceVisible(rect.index, cube.sizeBlocks))
        {
          continue;
        }
        const bool fullyOutside = rect.x1 <= 0 || rect.y1 <= 0 ||
                                  rect.x0 >= geometry.textureSize.x ||
                                  rect.y0 >= geometry.textureSize.y;
        if (fullyOutside)
        {
          F.Hard(id, "visible face " + std::to_string(rect.index) +
                         " UV rect fully outside texture: bone=" + bone.name +
                         " rect=" + std::to_string(rect.x0) + "," +
                         std::to_string(rect.y0) + ".." +
                         std::to_string(rect.x1) + "," +
                         std::to_string(rect.y1));
        }
        else if (rect.x0 < 0 || rect.y0 < 0 ||
                 rect.x1 > geometry.textureSize.x ||
                 rect.y1 > geometry.textureSize.y)
        {
          F.Warn(id, "visible face " + std::to_string(rect.index) +
                         " UV rect partially outside texture: bone=" +
                         bone.name);
        }
      }
    }
  }

  // Model-space AABB: bind pose = per-cube rest matrix composed with the
  // bone chain matrix (same path the runtime uses).
  cutum::BoneSkeletonHierarchy hierarchy(geometry);
  const cutum::BoneSkeletonPose emptyPose;
  glm::vec3 aabbMin(1e9f), aabbMax(-1e9f);
  for (size_t boneIdx = 0; boneIdx < geometry.bones.size(); ++boneIdx)
  {
    const glm::mat4 boneMat = hierarchy.ComputeBoneMatrix(boneIdx, emptyPose);
    for (const cutum::BoneSkeletonCubeDef &cube : geometry.bones[boneIdx].cubes)
    {
      glm::vec3 logical = cube.sizeBlocks;
      if (logical.x <= 0.f && logical.y <= 0.f && logical.z <= 0.f)
      {
        continue;
      }
      const float half = 0.5f;
      for (int i = 0; i < 8; ++i)
      {
        // Cube-local corner in blocks: originBlocks + corner*sizeBlocks.
        const glm::vec3 local =
            cube.originBlocks +
            glm::vec3(half * ((i & 1) ? 1.f : -1.f),
                      half * ((i & 2) ? 1.f : -1.f),
                      half * ((i & 4) ? 1.f : -1.f)) *
                cube.sizeBlocks;
        const glm::vec4 world = boneMat * glm::vec4(local, 1.f);
        aabbMin = glm::min(aabbMin, glm::vec3(world));
        aabbMax = glm::max(aabbMax, glm::vec3(world));
      }
    }
  }
  const glm::vec3 size = aabbMax - aabbMin;
  outMaxY = size.y;

  // Pose <-> rig: the locomotion cycle must move at least one bone that
  // really exists in this geometry, otherwise the mob slides with a frozen
  // body (the pose engine addresses bones by name and ignores unknown names).
  {
    cutum::CreatureLocomotionFacts facts;
    facts.horizontalSpeed = std::max(def.locomotion.walkSpeed, 0.5f);
    switch (def.locomotionArchetype)
    {
    case cutum::LocomotionArchetype::Aerial:
      facts.state = cutum::LocomotionState::Fly;
      break;
    case cutum::LocomotionArchetype::Aquatic:
      facts.state = cutum::LocomotionState::Swim;
      break;
    case cutum::LocomotionArchetype::Serpentine:
      facts.state = cutum::LocomotionState::Slither;
      break;
    default:
      facts.state = cutum::LocomotionState::Walk;
      break;
    }
    float maxSwing = 0.f;
    for (const float phase : {0.0f, 1.0f, 2.0f, 3.5f, 5.0f})
    {
      facts.animPhase = phase;
      const cutum::BoneSkeletonPose posed =
          cutum::BoneSkeletonPoseEngine::Compute(facts, def, 0.016f);
      for (const auto &entry : posed.bones)
      {
        if (geometry.boneIndexByName.find(entry.first) ==
            geometry.boneIndexByName.end())
        {
          continue;
        }
        // Rotation of a limb-like bone only: a body bob or head nod is not
        // limb animation. Fish/snakes undulate, so body/tail count for them.
        std::string lower = entry.first;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        bool limb = false;
        for (const char *key : {"leg", "foot", "haunch", "arm", "wing", "fin",
                                "tentacle"})
        {
          limb = limb || lower.find(key) != std::string::npos;
        }
        const bool undulates =
            def.locomotionArchetype == cutum::LocomotionArchetype::Aquatic ||
            def.locomotionArchetype == cutum::LocomotionArchetype::Serpentine;
        if (limb || undulates)
        {
          maxSwing = std::max(maxSwing, glm::length(entry.second.rotationDeg));
        }
      }
    }
    if (maxSwing < 5.0f)
    {
      F.Hard(id, "locomotion pose drives no bone of the rig (profile '" +
                     (def.visual.boneSkeleton.animationProfile.empty()
                          ? std::string("<archetype default>")
                          : def.visual.boneSkeleton.animationProfile) +
                     "' vs bones); mob would slide without limb animation");
    }
  }

  const glm::vec3 bounds = def.bounds.restSizeBlocks;
  const float modelMax = std::max({size.x, size.y, size.z});
  const float boundsMax = std::max({bounds.x, bounds.y, bounds.z});
  if (modelMax > 0.01f)
  {
    // Advisory only: this approximate AABB disagrees with known-good
    // species (cow/pig/chicken render correctly yet compute 1.3-1.7x);
    // collision bounds are a gameplay design parameter.
    const float ratio = modelMax / std::max(0.01f, boundsMax);
    if (ratio > 3.0f || ratio < 0.33f)
    {
      F.Warn(id, "bone visual size " + VecStr(size) + " vs bounds " +
                     VecStr(bounds) + " ratio=" + std::to_string(ratio));
    }
  }
}

void CheckGltf(const std::string &id, const fs::path &speciesDir,
               const cutum::CreatureDefinition &def, float &outMaxY)
{
  const std::string modelFile =
      def.visual.gltf.modelPath.empty() ? "model.gltf"
                                        : def.visual.gltf.modelPath;
  const fs::path modelPath = speciesDir / modelFile;
  if (!fs::exists(modelPath))
  {
    F.Hard(id, "gltf model missing: " + modelFile);
    return;
  }
  const fs::path binPath = fs::path(modelPath).replace_extension(".bin");
  if (!fs::exists(binPath))
  {
    F.Hard(id, "gltf bin missing: " + binPath.filename().string());
    return;
  }
  auto asset = cutum::CreatureGltfLoader::LoadFromFile(modelPath.string());
  if (!asset)
  {
    F.Hard(id, "gltf failed to parse: " + modelFile);
    return;
  }
  if (asset->primitives.empty())
  {
    F.Hard(id, "gltf has no primitives");
    return;
  }
  if (asset->hasSkin && asset->skin.jointNodes.size() > 96)
  {
    F.Hard(id, "gltf skin joints " +
                   std::to_string(asset->skin.jointNodes.size()) +
                   " exceed the 96-bone GPU palette cap");
  }

  // Model-space AABB from final vertex positions, scaled as at runtime.
  glm::vec3 aabbMin(1e9f), aabbMax(-1e9f);
  for (const cutum::GltfPrimitiveCpu &prim : asset->primitives)
  {
    for (size_t i = 0; i + 4 < prim.mesh.interleavedPosUv.size(); i += 5)
    {
      const glm::vec3 p(prim.mesh.interleavedPosUv[i],
                        prim.mesh.interleavedPosUv[i + 1],
                        prim.mesh.interleavedPosUv[i + 2]);
      aabbMin = glm::min(aabbMin, p);
      aabbMax = glm::max(aabbMax, p);
    }
  }
  const float scale = def.visual.gltf.modelScale;
  const glm::vec3 size = (aabbMax - aabbMin) * scale;
  outMaxY = size.y;

  const glm::vec3 bounds = def.bounds.restSizeBlocks;
  const float modelMax = std::max({size.x, size.y, size.z});
  const float boundsMax = std::max({bounds.x, bounds.y, bounds.z});
  if (modelMax > 0.01f)
  {
    const float ratio = modelMax / std::max(0.01f, boundsMax);
    if (ratio > 3.0f || ratio < 0.33f)
    {
      F.Hard(id, "visual size " + VecStr(size) + " vs bounds " +
                     VecStr(bounds) + " ratio=" + std::to_string(ratio));
    }
    else if (ratio > 1.6f || ratio < 0.62f)
    {
      F.Warn(id, "visual size " + VecStr(size) + " vs bounds " +
                     VecStr(bounds) + " ratio=" + std::to_string(ratio));
    }
  }

  // Textures referenced by primitives must exist.
  const fs::path texturesDir = speciesDir / "textures";
  for (const cutum::GltfPrimitiveCpu &prim : asset->primitives)
  {
    if (!TextureStemExists(texturesDir, prim.textureStem) &&
        !TextureStemExists(texturesDir, def.visual.defaultTextureKey))
    {
      F.Hard(id, "gltf texture missing: stem=" + prim.textureStem);
      break;
    }
  }

  // state_map targets must exist among the model's animations.
  for (const auto &entry : def.visual.Animation.stateMap)
  {
    if (asset->animationIndexByName.find(entry.second) ==
        asset->animationIndexByName.end())
    {
      F.Hard(id, "state_map '" + entry.first + "' -> missing clip '" +
                     entry.second + "'");
    }
  }
}

void CheckRigid(const std::string &id, const fs::path &speciesDir,
                const cutum::CreatureDefinition &def)
{
  const fs::path texturesDir = speciesDir / "textures";
  for (const cutum::CreatureVisualPartDef &part : def.visual.Parts)
  {
    if (glm::any(glm::lessThanEqual(part.sizeBlocks, glm::vec3(0.f))))
    {
      F.Hard(id, "rigid part '" + part.Id + "' has non-positive size");
    }
    const std::string stem =
        part.textureStem.empty() ? def.visual.defaultTextureKey
                                 : part.textureStem;
    if (!TextureStemExists(texturesDir, stem))
    {
      F.Hard(id, "rigid part '" + part.Id + "' texture missing: " + stem);
    }
  }
}

} // namespace

int main()
{
  const fs::path root =
      fs::path(CUBATARIUM_SOURCE_DIR) / "models" / "creatures";
  cutum::UCreatureDefinitionStorage storage;
  storage.Load(root.string());

  for (const std::string &id : storage.ListAllIds())
  {
    const cutum::CreatureDefinition *def = storage.Get(id);
    if (!def)
    {
      F.Hard(id, "definition failed to load");
      continue;
    }
    ++F.speciesChecked;
    const fs::path speciesDir = root / id;

    const cutum::CreatureVisualBackend backend =
        cutum::ResolveCreatureVisualBackend(*def);
    float modelMaxY = 0.f;
    switch (backend)
    {
    case cutum::CreatureVisualBackend::BoneSkeleton:
      CheckBoneSkeleton(id, speciesDir, *def, modelMaxY);
      break;
    case cutum::CreatureVisualBackend::GltfSkeleton:
      CheckGltf(id, speciesDir, *def, modelMaxY);
      break;
    default:
      CheckRigid(id, speciesDir, *def);
      break;
    }

    std::cout << (backend == cutum::CreatureVisualBackend::GltfSkeleton
                      ? "gltf   "
                      : (backend == cutum::CreatureVisualBackend::BoneSkeleton
                             ? "bones  "
                             : "rigid  "))
              << id << " bounds=" << VecStr(def->bounds.restSizeBlocks)
              << (modelMaxY > 0.f
                      ? " visualMaxY=" +
                            std::to_string(modelMaxY).substr(0, 5)
                      : "")
              << '\n';
  }

  std::cout << "\nchecked=" << F.speciesChecked
            << " hard=" << F.hardFailures << " warnings=" << F.warnings
            << '\n';
  return F.hardFailures == 0 ? 0 : 1;
}
