#ifndef CREATURE_QA_CAPTURE_H
#define CREATURE_QA_CAPTURE_H

#include "App/Core.h"
#include "App/Platform/CreatureQaMatrixCapture.h"
#include "App/Platform/CreatureQaEquipmentCapture.h"
#include "App/Platform/CreatureQaResourceCacheProbe.h"
#include "App/Platform/CreatureQaTextureMappingProbe.h"
#include "App/Platform/CreatureQaResourceReloadProbe.h"
#include "App/Platform/CreatureQaFpCapture.h"
#include "App/Platform/CreatureQaSaveProbe.h"
#include "App/Platform/CreatureQaEnvelopeCapture.h"
#include "App/Platform/CreatureQaTransitionProbe.h"
#include "App/Platform/CreatureQaFluidTransitionProbe.h"
#include "App/Platform/CreatureQaPosedCullingProbe.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Gui/Preview/CreaturePreviewRenderer.h"
#include "Creatures/Visual/Gltf/CreatureGltfCache.h"
#include "Creatures/Visual/Gltf/CreatureGltfBonePalette.h"
#include "Creatures/Visual/Gltf/CreatureAnimationClipMap.h"
#include "Creatures/Visual/WornEquipmentDrawer.h"
#include "Render/GlIncludes.h"
#include "Render/Pipeline/GlStateScope.h"
#include "ThirdParty/stb_image_write.h"
#include "World/Core/World.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace cutum
{
// Explicit flight-QA export from the same renderer used by dock and icons.
// Runs once with a current GL context; normal application runs do not call it.
inline void ExportCreatureQaFrames(UCore &core, UWorld &world,
                                  std::shared_ptr<UShaderManager> shaders)
{
  const char *directory = std::getenv("CUBA_FLIGHT_QA_PREVIEWS");
  if (directory == nullptr || directory[0] == '\0')
  {
    return;
  }
  const auto defs = world.GetCreatureDefinitionStorage();
  std::vector<std::string> ids;
  const char *selected = std::getenv("CUBA_FLIGHT_QA_PREVIEW_SPECIES");
  if (selected == nullptr) selected = std::getenv("CUBA_FLIGHT_QA_SPECIES");
  if (selected != nullptr)
  {
    std::istringstream stream(selected);
    std::string id;
    while (std::getline(stream, id, ',')) { ids.push_back(id); }
  }
  else { ids = defs->ListAllIds(); }
  UCreaturePreviewRenderer preview(defs, world.GetSkinDefinitionStorage(),
      core.GetCreatureTextureStorage(), shaders);
  if (!preview.Initialize())
  {
    throw std::runtime_error("QA preview renderer initialization failed");
  }
  std::filesystem::create_directories(directory);
  for (const std::string &id : ids)
  {
    for (int frame = 0; frame < (id == "human" ? 35 : 29); ++frame)
    {
      const int size = (frame == 12 || frame >= 33) ? 64 : 256;
      const bool walk = (frame >= 4 && frame < 29 && frame != 12) || frame == 31 || frame == 32;
      LocomotionState previewState = LocomotionState::Walk;
      int phaseIndex = frame - 4;
      if (frame >= 21)
      {
        previewState = LocomotionState::Action;
        phaseIndex = frame - 21;
      }
      else if (frame >= 13)
      {
        previewState = LocomotionState::Run;
        phaseIndex = frame - 13;
      }
      else if (const auto *def = defs->Get(id); def != nullptr)
      {
        if (def->habitat == CreatureHabitat::Aquatic)
          previewState = LocomotionState::Swim;
        else if (def->habitat == CreatureHabitat::Aerial)
          previewState = LocomotionState::Fly;
      }
      if (frame >= 4 && frame <= 11)
      {
        if (const char *state = std::getenv("CUBA_FLIGHT_QA_PRIMARY_STATE"))
        {
          const std::string selected(state);
          if (selected == "Swim") previewState = LocomotionState::Swim;
          else if (selected == "Fly") previewState = LocomotionState::Fly;
          else if (selected == "Glide") previewState = LocomotionState::Glide;
          else if (selected == "Hover") previewState = LocomotionState::Hover;
          else if (selected == "Walk") previewState = LocomotionState::Walk;
          else throw std::runtime_error("Unsupported QA primary state: " + selected);
        }
      }
      std::string skin;
      if (const char *selectedSkin = std::getenv("CUBA_FLIGHT_QA_SKIN"))
      {
        skin = selectedSkin;
      }
      std::string mainItem, offhandItem;
      std::array<WornArmorPreviewSlot, kWornArmorSlotCount> armor{};
      const bool equipment = frame == 31 || frame == 32;
      if (frame >= 29)
      {
        skin = (frame == 29 || frame == 31 || frame == 33) ? "human_adventurer" : "human_guard";
        previewState = LocomotionState::Walk;
        phaseIndex = 2;
        if (equipment)
        {
          const char *ids[] = {"copper_head", "copper_chest", "copper_legs", "copper_feet"};
          for (int i = 0; i < 4; ++i) { armor[i].ItemId = ids[i]; }
          mainItem = "wood_sword";
          offhandItem = "wood_torch";
        }
      }
      preview.SetAnimationPreviewState(previewState);
      const auto *definition = defs->Get(id);
      float period = 1.f / std::max(.1f, definition->visual.Animation.walkCycleHz);
      if (definition->visual.backend == "bone_skeleton" &&
          definition->visual.boneSkeleton.animationProfile == "aerial")
      {
        // ApplyAerial drives wings at four times the locomotion phase.
        // Sampling the slower cycle at eight phases aliases every wing pose
        // to a zero crossing and conceals the actual beat.
        period *= .25f;
      }
      if (definition->visual.backend == "gltf_skeleton")
      {
        const auto asset = CreatureGltfCache::Instance().Load(id, definition->visual.gltf.modelPath,definition->visual.sourceDirectory);
        const auto clip = ResolveAnimationClipId(previewState, definition->visual.Animation.stateMap);
        if (asset && clip)
        {
          const auto animation = asset->animationIndexByName.find(*clip);
          const auto settings = definition->visual.Animation.clips.find(*clip);
          if (animation != asset->animationIndexByName.end())
          {
            const float speed = settings == definition->visual.Animation.clips.end() ? 1.f : settings->second.speed;
            period = GltfAnimationDuration(asset->animations[animation->second]) / std::max(.01f, speed);
          }
        }
      }
      const float time = walk ? static_cast<float>(phaseIndex) * period /
          (previewState == LocomotionState::Action ? 7.f : 8.f) : 0.f;
      const float yaw = frame < 4 ? static_cast<float>(frame) * 90.f : 35.f;
      const GLuint texture = preview.RenderToUniqueTexture(id, skin, size,
          yaw, 15.f, time, walk, equipment ? &armor : nullptr,
          equipment ? &mainItem : nullptr, equipment ? &offhandItem : nullptr);
      if (texture == 0)
      {
        throw std::runtime_error("QA empty preview: " + id);
      }
      UGlStateScope state(kGlMaskIconFbo);
      GLuint fbo = 0;
      glGenFramebuffers(1, &fbo);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_2D, texture, 0);
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      {
        throw std::runtime_error("QA incomplete readback framebuffer");
      }
      std::vector<unsigned char> pixels(static_cast<size_t>(size) * size * 4);
      glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
      const size_t stride = static_cast<size_t>(size) * 4;
      for (int y = 0; y < size / 2; ++y)
      {
        std::swap_ranges(pixels.begin() + y * stride,
                         pixels.begin() + (y + 1) * stride,
                         pixels.begin() + (size - 1 - y) * stride);
      }
      const auto path = std::filesystem::path(directory) /
          (id + "_" + std::to_string(frame) + ".png");
      const bool saved = stbi_write_png(path.string().c_str(), size, size,
          4, pixels.data(), size * 4) != 0;
      glDeleteFramebuffers(1, &fbo);
      glDeleteTextures(1, &texture);
      if (!saved) { throw std::runtime_error("QA could not write " + path.string()); }
    }
  }
  ExportCreatureQaMatrixFrames(preview, ids, directory);
  ExportCreatureQaEquipmentFrames(preview, ids, directory);
  ExportCreatureQaResourceCacheProbe(preview, ids, directory);
  ExportCreatureQaTextureMappingProbe(preview, ids, directory);
  ExportCreatureQaResourceReloadProbe(preview, ids, directory);
  ExportCreatureQaFpFrames(core,world,shaders,directory);
  ExportCreatureQaSaveProbe(core,world,directory);
  ExportCreatureQaEnvelopes(preview,ids,directory);
  ExportCreatureQaTransitionProbe(preview,ids,directory);
  ExportCreatureQaFluidTransitionProbe(preview,ids,directory);
  ExportCreatureQaPosedCulling(preview,ids,directory);
}
} // namespace cutum
#endif
