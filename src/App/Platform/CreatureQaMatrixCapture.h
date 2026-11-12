#ifndef CREATURE_QA_MATRIX_CAPTURE_H
#define CREATURE_QA_MATRIX_CAPTURE_H

#include "Gui/Preview/CreaturePreviewRenderer.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"
#include "Creatures/Visual/Gltf/CreatureGltfCache.h"
#include "Creatures/Visual/Gltf/CreatureGltfBonePalette.h"
#include "Creatures/Visual/Gltf/CreatureAnimationClipMap.h"
#include "Render/GlIncludes.h"
#include "Render/Pipeline/GlStateScope.h"
#include "ThirdParty/stb_image_write.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace cutum
{
// Explicit QA only. Uses the actual preview controllers, loaders and draw sink.
// Alpha is retained from the real shader/discard coverage, never inferred from
// body color or a separately reconstructed mesh.
inline void ExportCreatureQaMatrixFrames(UCreaturePreviewRenderer &preview,
    const std::vector<std::string> &speciesIds, const std::filesystem::path &directory)
{
  const char *enabled = std::getenv("CUBA_FLIGHT_QA_MATRIX");
  if (enabled == nullptr || std::string(enabled) != "1") return;
  auto *defs = preview.GetSpecies();
  auto *skins = preview.GetSkins();
  struct Resource { std::string id, species, skin; };
  std::vector<Resource> resources;
  for (const auto &id : speciesIds) resources.push_back({id, id, ""});
  for (const auto &id : skins->ListEquippable())
  {
    const auto *skin = skins->Get(id);
    if (skin && std::find(speciesIds.begin(), speciesIds.end(), skin->creatureId) != speciesIds.end())
      resources.push_back({id, skin->creatureId, id});
  }
  nlohmann::json manifest = {{"schema", "creature-native-matrix.v1"},
      {"scope", "preview geometry/pose/coverage; not whole-world or performance acceptance"},
      {"resources", nlohmann::json::array()}};
  preview.SetTransparentBackground(true);
  constexpr int size = 256;
  for (const auto &resource : resources)
  {
    const auto *def = defs->Get(resource.species);
    if (!def) throw std::runtime_error("QA matrix unknown species: " + resource.species);
    LocomotionState primary = def->habitat == CreatureHabitat::Aquatic ? LocomotionState::Swim
        : def->habitat == CreatureHabitat::Aerial ? LocomotionState::Fly : LocomotionState::Walk;
    const std::string stateName = primary == LocomotionState::Swim ? "Swim"
        : primary == LocomotionState::Fly ? "Fly" : "Walk";
    float period = 1.f / std::max(.1f, def->visual.Animation.walkCycleHz);
    if (def->visual.backend == "bone_skeleton" && def->visual.boneSkeleton.animationProfile == "aerial")
      period *= .25f;
    std::string resolvedClip;
    bool explicitMapping = true, clipPresent = true;
    if (def->visual.backend == "gltf_skeleton")
    {
      const auto asset = CreatureGltfCache::Instance().Load(resource.species, def->visual.gltf.modelPath,def->visual.sourceDirectory);
      const auto clip = ResolveAnimationClipId(primary, def->visual.Animation.stateMap);
      if (!asset) throw std::runtime_error("QA matrix model missing: " + resource.species);
      // Match UCreatureVisualGltf exactly. Historical references may have no
      // mapping (runtime selects idle), or no such clip (runtime bind pose).
      // Preserve that evidence instead of inventing an animation for them.
      explicitMapping = clip.has_value();
      resolvedClip = clip.value_or("idle");
      clipPresent = asset->animationIndexByName.count(resolvedClip) != 0;
      const auto settings = def->visual.Animation.clips.find(resolvedClip);
      const float speed = settings == def->visual.Animation.clips.end() ? 1.f : settings->second.speed;
      if (clipPresent)
        period = GltfAnimationDuration(asset->animations[asset->animationIndexByName.at(resolvedClip)]) / std::max(.01f, speed);
    }
    const auto folder = directory / "matrix" / resource.id;
    std::filesystem::create_directories(folder);
    nlohmann::json row = {{"id", resource.id}, {"species", resource.species}, {"skin", resource.skin},
        {"backend", def->visual.backend}, {"primary_state", stateName}, {"primary_clip", resolvedClip},
        {"primary_mapping_explicit", explicitMapping}, {"primary_clip_present", clipPresent},
        {"primary_period_seconds", period}, {"frames", nlohmann::json::array()}};
    for (int pose = 0; pose < 3; ++pose)
    for (int angle = 0; angle < 10; ++angle)
    {
      // Opposite phases avoid sampling sin(2*pi*phase) twice at the same
      // value (1/8 and 3/8 would conceal a procedural leg/wing reversal).
      const float phase = pose == 0 ? 0.f : pose == 1 ? .125f : .625f;
      const float time = phase * period;
      const float yaw = angle < 8 ? angle * 45.f : 0.f;
      const float pitch = angle < 8 ? 15.f : angle == 8 ? 90.f : -90.f;
      preview.SetAnimationPreviewState(primary);
      const GLuint texture = preview.RenderToUniqueTexture(resource.species, resource.skin,
          size, yaw, pitch, time, pose != 0);
      if (!texture) throw std::runtime_error("QA matrix empty render: " + resource.id);
      UGlStateScope state(kGlMaskIconFbo);
      GLuint fbo = 0;
      glGenFramebuffers(1, &fbo);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("QA matrix incomplete framebuffer");
      std::vector<unsigned char> pixels(size * size * 4);
      glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
      for (int y = 0; y < size / 2; ++y)
        std::swap_ranges(pixels.begin() + y * size * 4, pixels.begin() + (y + 1) * size * 4,
            pixels.begin() + (size - 1 - y) * size * 4);
      const std::string stem = "pose_" + std::to_string(pose) + "_view_" + std::to_string(angle);
      const auto color = folder / (stem + "_color.png");
      const auto silhouette = folder / (stem + "_silhouette.png");
      bool saved = stbi_write_png(color.string().c_str(), size, size, 4, pixels.data(), size * 4) != 0;
      for (size_t i = 0; i < pixels.size(); i += 4)
      {
        const unsigned char value = pixels[i + 3] > 0 ? 0 : 255;
        pixels[i] = pixels[i + 1] = pixels[i + 2] = value;
        pixels[i + 3] = 255;
      }
      saved = saved && stbi_write_png(silhouette.string().c_str(), size, size, 4, pixels.data(), size * 4) != 0;
      glDeleteFramebuffers(1, &fbo);
      glDeleteTextures(1, &texture);
      if (!saved) throw std::runtime_error("QA matrix write failed: " + resource.id);
      row["frames"].push_back({{"pose", pose}, {"view", angle}, {"state", pose == 0 ? "Idle" : stateName},
          {"phase", phase}, {"time_seconds", time}, {"yaw_degrees", yaw}, {"pitch_degrees", pitch},
          {"color", (resource.id + "/" + color.filename().string())},
          {"silhouette", (resource.id + "/" + silhouette.filename().string())}});
    }
    manifest["resources"].push_back(row);
  }
  preview.SetTransparentBackground(false);
  std::ofstream stream(directory / "matrix" / "manifest.json");
  stream << manifest.dump(2) << '\n';
  if (!stream) throw std::runtime_error("QA matrix manifest write failed");
}
} // namespace cutum
#endif
