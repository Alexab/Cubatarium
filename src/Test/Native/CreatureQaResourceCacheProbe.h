#pragma once
#include "App/Settings/RenderSettings.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"
#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonCache.h"
#include "Creatures/Visual/CreatureAppearance.h"
#include "Creatures/Visual/CreatureDrawQueue.h"
#include "Creatures/Visual/CreatureRigidModelLoader.h"
#include "Creatures/Visual/CreatureTextureStorage.h"
#include "Creatures/Visual/CreatureVisualFactory.h"
#include "Creatures/Visual/Gltf/CreatureAnimationClipMap.h"
#include "Creatures/Visual/Gltf/CreatureGltfBonePalette.h"
#include "Creatures/Visual/Gltf/CreatureGltfCache.h"
#include "Creatures/Visual/Gltf/CreatureGltfClipClock.h"
#include "Gui/Preview/CreaturePreviewRenderer.h"
#include "ResourcePacks/CreaturePackMerge.h"
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace cutum
{
// Observe the actual world/preview submission path without replacing visuals
// with a test implementation. Pointer equality is against a fresh visual that
// holds the newly cached asset alive, so allocator reuse cannot fake a pass.
class UCreatureQaResourceDrawSnapshot : public ICreatureDrawSink
{
public:
  uint64_t hash{14695981039346656037ull};
  size_t draws{0};
  std::vector<const void *> meshes;
  std::vector<float> poseValues;
  std::vector<GLuint> textures;
  std::vector<uint64_t> geometryHashes;
  void Geometry(const void *data, size_t bytes)
  {
    const auto *p = static_cast<const unsigned char *>(data);
    for (size_t i = 0; i < bytes; ++i)
    {
      geometryHashes.back() ^= p[i];
      geometryHashes.back() *= 1099511628211ull;
    }
  }
  void Add(const void *data, size_t bytes)
  {
    const auto *p = static_cast<const unsigned char *>(data);
    for (size_t i = 0; i < bytes; ++i)
    {
      hash ^= p[i];
      hash *= 1099511628211ull;
    }
  }
  void Common(const glm::mat4 &mvp, GLuint texture)
  {
    ++draws;
    textures.push_back(texture);
    Add(&mvp, sizeof(mvp));
    Add(&texture, sizeof(texture));
    geometryHashes.push_back(14695981039346656037ull);
    Geometry(&mvp, sizeof(mvp));
    const auto *values = &mvp[0][0];
    poseValues.insert(poseValues.end(), values, values + 16);
  }
  void DrawTexturedPart(const glm::mat4 &mvp, GLuint texture,
                        CreaturePartMesh mesh) override
  {
    Common(mvp, texture);
    Add(&mesh, sizeof(mesh));
    Geometry(&mesh, sizeof(mesh));
  }
  void DrawBoneSkeletonMesh(const glm::mat4 &mvp, GLuint texture,
                            const BoneSkeletonCubeMeshCpu &mesh) override
  {
    Common(mvp, texture);
    meshes.push_back(&mesh);
    Add(mesh.interleavedPosUv.data(),
        mesh.interleavedPosUv.size() * sizeof(float));
    Add(mesh.indices.data(), mesh.indices.size() * sizeof(unsigned));
    Geometry(mesh.interleavedPosUv.data(),
             mesh.interleavedPosUv.size() * sizeof(float));
    Geometry(mesh.indices.data(), mesh.indices.size() * sizeof(unsigned));
  }
  void DrawSkinnedMesh(
      const glm::mat4 &mvp, GLuint texture, const GltfPrimitiveCpu &mesh,
      const std::shared_ptr<const std::vector<glm::mat4>> &bones) override
  {
    DrawBoneSkeletonMesh(mvp, texture, mesh.mesh);
    if (bones)
    {
      Add(bones->data(), bones->size() * sizeof(glm::mat4));
      Geometry(bones->data(), bones->size() * sizeof(glm::mat4));
      for (const auto &matrix : *bones)
      {
        const auto *values = &matrix[0][0];
        poseValues.insert(poseValues.end(), values, values + 16);
      }
    }
  }
  void DrawBoxWireframe(const glm::mat4 &, const glm::vec4 &) override {}
  void DrawBillboard(const glm::mat4 &mvp, GLuint texture,
                     const glm::vec4 &tint) override
  {
    Common(mvp, texture);
    Add(&tint, sizeof(tint));
  }
};

inline void
ExportCreatureQaResourceCacheProbe(UCreaturePreviewRenderer &preview,
                                   const std::vector<std::string> &ids,
                                   const std::filesystem::path &directory)
{
  const char *enabled = std::getenv("CUBA_FLIGHT_QA_RESOURCE_CACHE_PROBE");
  if (!enabled || std::string(enabled) != "1")
    return;
  using Json = nlohmann::json;
  Json report = {
      {"schema", "creature-live-resource-cache-probe.v1"},
      {"scope",
       "Actual live/fresh draw submissions after same-root mesh cache reset; "
       "not artistic, full pack/texture reload or performance acceptance"},
      {"resources", Json::array()},
      {"resource_path_cases", Json::array()},
      {"resource_directory_cases", Json::array()}};
  RenderSettings settings;
  settings.CreatureTexturedParts = true;
  settings.CreatureWireframeOverlay = false;
  bool passed = true;
  Json skinReport = {
      {"schema", "creature-skin-ownership-probe.v1"},
      {"scope",
       "Actual GPU texture IDs in visual submissions for raw and resolved-only "
       "skin selection; not full texture-map/FP/save/pack acceptance"},
      {"cases", Json::array()}};
  bool skinsPassed = true;
  for (const auto &id : ids)
  {
    const auto *definition = preview.GetSpecies()->Get(id);
    if (!definition)
      throw std::runtime_error("Resource probe definition missing: " + id);
    const auto &def = *definition;
    UCreature creature(1, id, glm::vec3(0.f),
                       glm::vec3(0.f, def.eyeHeight, 0.f));
    creature.GetBoundsMutable().profile = def.bounds;
    creature.GetBoundsMutable().currentSizeBlocks = def.bounds.restSizeBlocks;
    CreatureLocomotionFacts facts;
    facts.state = LocomotionState::Walk;
    facts.horizontalSpeed = def.locomotion.walkSpeed;
    facts.animPhase = .375f;
    const auto appearance = ResolveCreatureAppearance(
        *preview.GetSpecies(), *preview.GetSkins(), id, "");
    auto live = CreateCreatureVisual(def);
    live->SetAppearance(appearance);
    live->UpdatePose(creature, facts, CreaturePoseParams{}, def, .1375f);
    auto snapshot = [&](IUCreatureVisual &visual)
    {
      CreatureDrawQueue queue;
      CreatureDrawContext context;
      context.Queue = &queue;
      context.Textures = preview.GetTextures();
      context.Settings = &settings;
      visual.SubmitDraw(context, glm::mat4(1.f));
      UCreatureQaResourceDrawSnapshot result;
      queue.Flush(result);
      return result;
    };
    const auto before = snapshot(*live);
    for (const auto &skinId : preview.GetSkins()->ListEquippable())
    {
      const auto *skin = preview.GetSkins()->Get(skinId);
      if (!skin)
        throw std::runtime_error("Missing skin definition");
      const bool compatible =
          skin->creatureId.empty() || skin->creatureId == id;
      for (const bool rawSelection : {true, false})
      {
        creature.SetSkinId(rawSelection ? skinId : std::string());
        auto selected = CreateCreatureVisual(def);
        selected->SetAppearance(ResolveCreatureAppearance(
            *preview.GetSpecies(), *preview.GetSkins(), id, skinId));
        selected->UpdatePose(creature, facts, CreaturePoseParams{}, def,
                             .1375f);
        const auto drawn = snapshot(*selected);
        // All five bundled compatible skins are B atlases. Reject an expanded
        // catalog here until a corresponding independent oracle is supplied.
        if (compatible && def.visual.backend != "bone_skeleton")
          throw std::runtime_error(
              "Skin probe needs oracle for non-B compatible skin");
        const GLuint wanted = compatible ? preview.GetTextures()->GetTexture(
                                               "skin/" + skinId + "/diffuse")
                                         : 0;
        const bool skinValid =
            drawn.draws == before.draws && drawn.draws > 0 &&
            (compatible
                 ? (wanted != 0 &&
                    drawn.textures == std::vector<GLuint>(drawn.draws, wanted))
                 : drawn.hash == before.hash);
        skinsPassed = skinsPassed && skinValid;
        skinReport["cases"].push_back({{"species", id},
                                       {"skin", skinId},
                                       {"compatible", compatible},
                                       {"raw_selection", rawSelection},
                                       {"draws", drawn.draws},
                                       {"textures", drawn.textures},
                                       {"base_textures", before.textures},
                                       {"base_hash", before.hash},
                                       {"actual_hash", drawn.hash},
                                       {"expected_skin_texture", wanted},
                                       {"passed", skinValid}});
      }
    }
    creature.SetSkinId("");
    auto &boneCache = CreatureBoneSkeletonCache::Instance();
    auto &gltfCache = CreatureGltfCache::Instance();
    boneCache.SetCreaturesRoot(boneCache.GetCreaturesRoot());
    gltfCache.Clear();
    auto fresh = CreateCreatureVisual(def);
    fresh->SetAppearance(appearance);
    fresh->UpdatePose(creature, facts, CreaturePoseParams{}, def, .1375f);
    const auto expected = snapshot(*fresh);
    live->UpdatePose(creature, facts, CreaturePoseParams{}, def, 0.f);
    const auto actual = snapshot(*live);
    const bool valid = before.draws > 0 && actual.draws == expected.draws &&
                       actual.hash == expected.hash &&
                       actual.meshes == expected.meshes;
    passed = passed && valid;
    report["resources"].push_back(
        {{"id", id},
         {"backend", def.visual.backend},
         {"before_draws", before.draws},
         {"live_draws", actual.draws},
         {"fresh_draws", expected.draws},
         {"before_hash", before.hash},
         {"live_hash", actual.hash},
         {"fresh_hash", expected.hash},
         {"live_uses_fresh_meshes", actual.meshes == expected.meshes},
         {"passed", valid}});
    if (def.visual.backend == "bone_skeleton" ||
        def.visual.backend == "gltf_skeleton")
    {
      auto changed = def;
      std::string alternateDirectory;
      float referenceDt = .1375f;
      if (def.visual.backend == "gltf_skeleton")
      {
        const auto originalMesh = gltfCache.Load(id, def.visual.gltf.modelPath,
                                                 def.visual.sourceDirectory);
        const auto name =
            ResolveAnimationClipId(facts.state, def.visual.Animation.stateMap)
                .value_or("idle");
        float playback = 1.f;
        bool loop = true;
        if (const auto clip = def.visual.Animation.clips.find(name);
            clip != def.visual.Animation.clips.end())
        {
          playback = clip->second.speed;
          loop = clip->second.loop;
        }
        float travel = std::clamp(facts.horizontalSpeed /
                                      std::max(def.locomotion.walkSpeed, .05f),
                                  0.f, 1.5f);
        travel = travel < .04f ? 0.f : std::max(.22f, travel);
        playback *= travel;
        float duration = 0.f;
        if (originalMesh)
          if (const auto clip = originalMesh->animationIndexByName.find(name);
              clip != originalMesh->animationIndexByName.end())
            duration =
                GltfAnimationDuration(originalMesh->animations[clip->second]);
        // Retain OLD clip-local time when content changes. A fresh target
        // started with the same wall dt is a wrong oracle if durations differ.
        const float oldTime =
            WrapGltfClipTime(.1375f * playback, duration, loop);
        referenceDt = playback > 0.f ? oldTime / playback : 0.f;
      }
      auto poseError = [](const UCreatureQaResourceDrawSnapshot &a,
                          const UCreatureQaResourceDrawSnapshot &b)
      {
        if (a.poseValues.size() != b.poseValues.size())
          return 1e9f;
        float result = 0.f;
        for (size_t i = 0; i < a.poseValues.size(); ++i)
        {
          if (!std::isfinite(a.poseValues[i]) ||
              !std::isfinite(b.poseValues[i]))
            return 1e9f;
          result =
              std::max(result, std::abs(a.poseValues[i] - b.poseValues[i]));
        }
        return result;
      };
      if (def.visual.backend == "bone_skeleton")
      {
        if (std::filesystem::path(id) != std::filesystem::path(id).filename())
          throw std::runtime_error("Unsafe probe species path");
        const auto source = std::filesystem::path(def.visual.sourceDirectory) /
                            def.visual.boneSkeleton.geometryFile;
        std::ifstream input(source);
        Json fixture;
        input >> fixture;
        bool modified = false;
        for (auto &entry : fixture.items())
        {
          if (!entry.value().is_object() || !entry.value().contains("bones"))
            continue;
          for (auto &bone : entry.value()["bones"])
          {
            if (!bone.contains("cubes") || bone["cubes"].empty())
              continue;
            auto &x = bone["cubes"][0]["origin"][0];
            x = x.get<double>() + 16.;
            modified = true;
            break;
          }
          if (modified)
            break;
        }
        if (!modified)
          throw std::runtime_error("No diagnostic cube in " + id);
        const auto folder = directory / "resource_cache_fixture" / id;
        std::filesystem::create_directories(folder);
        const auto file = std::filesystem::absolute(
            folder / std::filesystem::path(def.visual.boneSkeleton.geometryFile)
                         .filename());
        std::ofstream output(file);
        output << fixture.dump(2) << '\n';
        output.close();
        if (!output)
          throw std::runtime_error("Could not write diagnostic geometry");
        changed.visual.boneSkeleton.geometryFile = file.string();
        alternateDirectory = file.parent_path().string();
      }
      else
      {
        const CreatureDefinition *alternate = nullptr;
        for (const auto &candidateId : preview.GetSpecies()->ListAllIds())
        {
          const auto *candidate = preview.GetSpecies()->Get(candidateId);
          if (candidateId != id && candidate &&
              candidate->visual.backend == "gltf_skeleton" &&
              candidate->visual.gltf.modelPath == def.visual.gltf.modelPath)
          {
            alternate = candidate;
            break;
          }
        }
        if (!alternate)
          throw std::runtime_error("Missing alternate probe model");
        alternateDirectory = alternate->visual.sourceDirectory;
        changed.visual.gltf.modelPath =
            (std::filesystem::path(alternate->visual.sourceDirectory) /
             alternate->visual.gltf.modelPath)
                .string();
      }
      auto changedFresh = CreateCreatureVisual(changed);
      changedFresh->SetAppearance(appearance);
      changedFresh->UpdatePose(creature, facts, CreaturePoseParams{}, changed,
                               referenceDt);
      const auto changedExpected = snapshot(*changedFresh);
      live->UpdatePose(creature, facts, CreaturePoseParams{}, changed, 0.f);
      const auto changedActual = snapshot(*live);
      const float changedPoseError = poseError(changedActual, changedExpected);
      const bool changedValid = changedExpected.draws > 0 &&
                                changedExpected.hash != before.hash &&
                                changedActual.draws == changedExpected.draws &&
                                changedPoseError <= 5e-6f &&
                                changedActual.meshes == changedExpected.meshes;
      passed = passed && changedValid;
      report["resource_path_cases"].push_back(
          {{"id", id},
           {"backend", def.visual.backend},
           {"same_species", true},
           {"same_geometry_id", changed.visual.boneSkeleton.geometryId ==
                                    def.visual.boneSkeleton.geometryId},
           {"resource", def.visual.backend == "bone_skeleton"
                            ? changed.visual.boneSkeleton.geometryFile
                            : changed.visual.gltf.modelPath},
           {"changed_content_hash", changedExpected.hash},
           {"live_hash", changedActual.hash},
           {"reference_dt", referenceDt},
           {"max_pose_matrix_error", changedPoseError},
           {"live_uses_fresh_meshes",
            changedActual.meshes == changedExpected.meshes},
           {"passed", changedValid}});
      auto directoryDef = def;
      directoryDef.visual.sourceDirectory = alternateDirectory;
      auto directoryLive = CreateCreatureVisual(def);
      directoryLive->SetAppearance(appearance);
      directoryLive->UpdatePose(creature, facts, CreaturePoseParams{}, def,
                                .1375f);
      auto directoryFresh = CreateCreatureVisual(directoryDef);
      directoryFresh->SetAppearance(appearance);
      directoryFresh->UpdatePose(creature, facts, CreaturePoseParams{},
                                 directoryDef, referenceDt);
      const auto directoryExpected = snapshot(*directoryFresh);
      directoryLive->UpdatePose(creature, facts, CreaturePoseParams{},
                                directoryDef, 0.f);
      const auto directoryActual = snapshot(*directoryLive);
      const float directoryPoseError =
          poseError(directoryActual, directoryExpected);
      const bool directoryValid =
          directoryExpected.draws > 0 &&
          directoryExpected.hash != before.hash &&
          directoryActual.draws == directoryExpected.draws &&
          directoryPoseError <= 5e-6f &&
          directoryActual.meshes == directoryExpected.meshes;
      passed = passed && directoryValid;
      report["resource_directory_cases"].push_back(
          {{"id", id},
           {"backend", def.visual.backend},
           {"source_directory", alternateDirectory},
           {"same_species_and_filename", true},
           {"changed_content_hash", directoryExpected.hash},
           {"live_hash", directoryActual.hash},
           {"reference_dt", referenceDt},
           {"max_pose_matrix_error", directoryPoseError},
           {"live_uses_fresh_meshes",
            directoryActual.meshes == directoryExpected.meshes},
           {"passed", directoryValid}});
    }
  }
  // Exercise the real pack-merge entry point, not a stand-in cache clear.
  // Rigid parts are parsed into definitions, so invalidating after Load is too
  // late.
  if (const auto *original = preview.GetSpecies()->Get("rigid_demo_walker"))
  {
    const auto root = directory / "resource_cache_fixture" / "rigid_reload";
    const auto folder = root / "creatures" / original->Id;
    std::filesystem::create_directories(folder);
    const auto source = std::filesystem::path(original->visual.sourceDirectory);
    std::filesystem::copy_file(
        source / "creature.json", folder / "creature.json",
        std::filesystem::copy_options::overwrite_existing);
    std::ifstream input(source / original->visual.rigidModelPath);
    Json model;
    input >> model;
    const auto target = folder / original->visual.rigidModelPath;
    {
      std::ofstream file(target);
      file << model.dump(2);
    }
    CreatureRigidModelCache::Instance().Clear();
    UCreatureDefinitionStorage definitions;
    definitions.Load((root / "creatures").string());
    const auto oldParts = definitions.Get(original->Id)->visual.Parts;
    model["parts"][0]["size"][0] =
        model["parts"][0]["size"][0].get<float>() * 1.25f;
    {
      std::ofstream file(target);
      file << model.dump(2);
    }
    struct RestoreRoots
    {
      std::string bone, gltf;
      ~RestoreRoots()
      {
        CreatureBoneSkeletonCache::Instance().SetCreaturesRoot(bone);
        CreatureGltfCache::Instance().SetCreaturesRoot(gltf);
      }
    } restore{CreatureBoneSkeletonCache::Instance().GetCreaturesRoot(),
              CreatureGltfCache::Instance().GetCreaturesRoot()};
    UCreatureTextureStorage unusedTextures;
    ApplyCreaturePackOverlays(definitions, unusedTextures, nullptr,
                              root / "creatures", root / "skins", {});
    const auto &reloaded = definitions.Get(original->Id)->visual.Parts;
    const bool rigidValid =
        !oldParts.empty() && reloaded.size() == oldParts.size() &&
        std::abs(reloaded[0].sizeBlocks.x - oldParts[0].sizeBlocks.x * 1.25f) <
            1e-6f;
    report["rigid_pack_reload"] = {{"id", original->Id},
                                   {"before_width", oldParts[0].sizeBlocks.x},
                                   {"reloaded_width", reloaded[0].sizeBlocks.x},
                                   {"passed", rigidValid}};
    passed = passed && rigidValid;
  }
  report["passed"] = passed;
  skinReport["passed"] = skinsPassed;
  {
    std::ofstream stream(directory / "appearance_skin_probe.json");
    stream << skinReport.dump(2) << '\n';
    if (!stream)
      throw std::runtime_error("Skin ownership report failed");
  }
  std::ofstream stream(directory / "resource_cache_probe.json");
  stream << report.dump(2) << '\n';
  stream.close();
  if (!stream)
    throw std::runtime_error("Resource cache probe report failed");
  if (!passed)
    throw std::runtime_error(
        "Live creature visuals retained stale meshes after cache reset");
  if (!skinsPassed)
    throw std::runtime_error(
        "Creature skin ownership or resolved selection mismatch");
}
} // namespace cutum
