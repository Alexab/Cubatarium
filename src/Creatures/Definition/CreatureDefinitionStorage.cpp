#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Core/CreatureCatalogTypes.h"
#include "Creatures/Locomotion/LocomotionTypes.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <glm/glm.hpp>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

namespace
{

glm::vec3 ReadVec3(const nlohmann::json &arr, const glm::vec3 &fallback)
{
  if (!arr.is_array() || arr.size() < 3)
  {
    return fallback;
  }
  return glm::vec3(arr[0].get<float>(), arr[1].get<float>(),
                   arr[2].get<float>());
}

glm::vec4 ReadVec4(const nlohmann::json &arr, const glm::vec4 &fallback)
{
  if (!arr.is_array() || arr.size() < 4)
  {
    return fallback;
  }
  return glm::vec4(arr[0].get<float>(), arr[1].get<float>(),
                   arr[2].get<float>(), arr[3].get<float>());
}

CreatureAnimationClipDef ReadClipDef(const nlohmann::json &clipJson)
{
  CreatureAnimationClipDef clip;
  clip.startSec = clipJson.value("start", clip.startSec);
  clip.endSec = clipJson.value("end", clip.endSec);
  clip.loop = clipJson.value("loop", clip.loop);
  clip.speed = clipJson.value("speed", clip.speed);
  return clip;
}

} // namespace

void UCreatureDefinitionStorage::Load(const std::string &folder)
{
  Definitions.clear();
  if (!std::filesystem::exists(folder))
  {
    return;
  }
  for (const auto &entry : std::filesystem::directory_iterator(folder))
  {
    if (!entry.is_directory())
    {
      continue;
    }
    const std::filesystem::path jsonPath = entry.path() / "creature.json";
    if (std::filesystem::exists(jsonPath))
    {
      LoadFile(jsonPath.string());
    }
  }
  std::cout << "UCreatureDefinitionStorage: loaded " << Definitions.size()
            << " definitions" << std::endl;
}

void UCreatureDefinitionStorage::LoadOverlay(const std::string &folder)
{
  if (!std::filesystem::exists(folder))
  {
    return;
  }
  size_t overlayCount = 0;
  for (const auto &entry : std::filesystem::directory_iterator(folder))
  {
    if (!entry.is_directory())
    {
      continue;
    }
    const std::filesystem::path jsonPath = entry.path() / "creature.json";
    if (std::filesystem::exists(jsonPath) && LoadFile(jsonPath.string()))
    {
      ++overlayCount;
    }
  }
  if (overlayCount > 0)
  {
    std::cout << "UCreatureDefinitionStorage: overlay " << overlayCount
              << " definition(s) from " << folder << std::endl;
  }
}

bool UCreatureDefinitionStorage::LoadFile(const std::string &path)
{
  try
  {
    std::ifstream file(path);
    if (!file.is_open())
    {
      return false;
    }
    nlohmann::json data;
    file >> data;
    CreatureDefinition def;
    def.Id = data.value("id", "");
    if (def.Id.empty())
    {
      return false;
    }
    def.displayName = data.value("display_name", def.Id);
    if (data.contains("catalog") && data["catalog"].is_object())
    {
      const auto &catalog = data["catalog"];
      if (catalog.contains("tags") && catalog["tags"].is_array())
      {
        for (const auto &tag : catalog["tags"])
        {
          if (tag.is_string())
          {
            def.catalog.tags.push_back(tag.get<std::string>());
          }
        }
      }
      def.catalog.spawnable = catalog.value("spawnable", false);
      def.catalog.sortOrder = catalog.value("sort_order", 0);
    }
    def.role = ParseCreatureRole(data.value("role", ""));
    if (data.contains("bounds"))
    {
      const auto &b = data["bounds"];
      def.bounds.restSizeBlocks = ReadVec3(
          b.value("rest", nlohmann::json::array()), def.bounds.restSizeBlocks);
      def.bounds.maxSizeBlocks = ReadVec3(
          b.value("max", nlohmann::json::array()), def.bounds.maxSizeBlocks);
      def.bounds.minSizeBlocks = ReadVec3(
          b.value("min", nlohmann::json::array()), def.bounds.minSizeBlocks);
    }
    def.eyeHeight = data.value("eye_height", def.eyeHeight);
    def.locomotionArchetype = ParseLocomotionArchetype(
        data.value("locomotion_archetype", "terrestrial_biped"));
    def.behavior.Id = data.value("behavior", def.behavior.Id);
    if (data.contains("behavior_params") && data["behavior_params"].is_object())
    {
      const auto &bp = data["behavior_params"];
      def.behavior.moveSpeed = bp.value("move_speed", def.behavior.moveSpeed);
      def.behavior.wanderIntervalMin =
          bp.value("wander_interval_min", def.behavior.wanderIntervalMin);
      def.behavior.wanderIntervalMax =
          bp.value("wander_interval_max", def.behavior.wanderIntervalMax);
    }
    if (data.contains("locomotion"))
    {
      const auto &loc = data["locomotion"];
      def.locomotion.canFly = loc.value("can_fly", true);
      def.locomotion.canCrouch = loc.value("can_crouch", true);
      def.locomotion.canJump = loc.value("can_jump", true);
      def.locomotion.jumpHeightBlocks =
          loc.value("jump_height", def.locomotion.jumpHeightBlocks);
      const bool hasWalkSpeed = loc.contains("walk_speed");
      def.locomotion.walkSpeed =
          loc.value("walk_speed", hasWalkSpeed ? def.locomotion.walkSpeed
                                               : def.behavior.moveSpeed);
      def.locomotion.flySpeed =
          loc.value("fly_speed", def.locomotion.walkSpeed);
    }
    else
    {
      def.locomotion.walkSpeed = def.behavior.moveSpeed;
      def.locomotion.flySpeed = def.locomotion.walkSpeed;
    }
    if (data.contains("visual") && data["visual"].is_object())
    {
      const auto &vis = data["visual"];
      def.visual.backend = vis.value("backend", def.visual.backend);
      def.visual.fallbackBackend = vis.value("fallback_backend", "");
      const CreatureVisualBackend parsedBackend =
          ParseCreatureVisualBackend(def.visual.backend);
      def.visual.backend = ToString(parsedBackend);
      def.visual.modelYawOffsetDeg = vis.value("model_yaw_offset_deg", 0.f);
      def.visual.textureLayout =
          vis.value("texture_layout", def.visual.textureLayout);
      def.visual.defaultTextureKey =
          vis.value("default_texture", def.visual.defaultTextureKey);
      if (vis.contains("icon") && vis["icon"].is_object())
      {
        const auto &icon = vis["icon"];
        def.visual.iconMode = icon.value("mode", def.visual.iconMode);
        def.visual.wireframeColor =
            ReadVec4(icon.value("color", nlohmann::json::array()),
                     def.visual.wireframeColor);
      }
      if (vis.contains("animation") && vis["animation"].is_object())
      {
        const auto &anim = vis["animation"];
        def.visual.Animation.walkCycleHz =
            anim.value("walk_cycle_hz", def.visual.Animation.walkCycleHz);
        def.visual.Animation.legSwingDeg =
            anim.value("leg_swing_deg", def.visual.Animation.legSwingDeg);
        def.visual.Animation.armSwingDeg =
            anim.value("arm_swing_deg", def.visual.Animation.armSwingDeg);
        def.visual.Animation.flyBodyPitchDeg = anim.value(
            "fly_body_pitch_deg", def.visual.Animation.flyBodyPitchDeg);
        if (anim.contains("clips") && anim["clips"].is_object())
        {
          for (const auto &[clipId, clipJson] : anim["clips"].items())
          {
            if (clipJson.is_object())
            {
              def.visual.Animation.clips[clipId] = ReadClipDef(clipJson);
            }
          }
        }
        if (anim.contains("state_map") && anim["state_map"].is_object())
        {
          for (const auto &[stateName, clipId] : anim["state_map"].items())
          {
            if (clipId.is_string())
            {
              def.visual.Animation.stateMap[stateName] = clipId.get<std::string>();
            }
          }
        }
      }
      if (vis.contains("gltf") && vis["gltf"].is_object())
      {
        const auto &gltf = vis["gltf"];
        def.visual.gltf.modelPath = gltf.value("model", "");
        def.visual.gltf.modelScale = gltf.value("model_scale", 1.f);
        def.visual.gltf.modelYawOffsetDeg =
            gltf.value("model_yaw_offset_deg", 0.f);
        if (gltf.contains("textures") && gltf["textures"].is_array())
        {
          for (const auto &tex : gltf["textures"])
          {
            if (tex.is_string())
            {
              def.visual.gltf.texturePaths.push_back(tex.get<std::string>());
            }
          }
        }
      }
      if (vis.contains("rig") && vis["rig"].is_object())
      {
        const auto &rig = vis["rig"];
        def.visual.rig.templateId =
            rig.value("template", def.visual.rig.templateId);
        if (rig.contains("parts") && rig["parts"].is_array())
        {
          for (const auto &partId : rig["parts"])
          {
            if (partId.is_string())
            {
              def.visual.rig.partIds.push_back(partId.get<std::string>());
            }
          }
        }
      }
      if (vis.contains("parts") && vis["parts"].is_array())
      {
        for (const auto &partJson : vis["parts"])
        {
          CreatureVisualPartDef part;
          part.Id = partJson.value("id", "");
          part.textureStem =
              partJson.value("texture", def.visual.defaultTextureKey);
          if (part.textureStem.empty())
          {
            part.textureStem = "body";
          }
          part.offsetBlocks =
              ReadVec3(partJson.value("offset", nlohmann::json::array()),
                       part.offsetBlocks);
          part.sizeBlocks = ReadVec3(
              partJson.value("size", nlohmann::json::array()), part.sizeBlocks);
          part.HasPivot = partJson.contains("pivot");
          if (part.HasPivot)
          {
            part.PivotBlocks =
                ReadVec3(partJson.value("pivot", nlohmann::json::array()),
                         part.PivotBlocks);
          }
          part.LimbKind = partJson.value("limb", "");
          part.LimbAxis = partJson.value("limb_axis", "x");
          def.visual.Parts.push_back(part);
        }
      }
      if (parsedBackend == CreatureVisualBackend::GltfSkeleton &&
          def.visual.gltf.modelPath.empty())
      {
        std::cerr << "UCreatureDefinitionStorage: " << path
                  << ": gltf_skeleton without visual.gltf.model for "
                  << def.Id << std::endl;
      }
    }
    Definitions[def.Id] = def;
    return true;
  }
  catch (const std::exception &e)
  {
    std::cerr << "UCreatureDefinitionStorage: " << path << ": " << e.what()
              << std::endl;
    return false;
  }
}

const CreatureDefinition *
UCreatureDefinitionStorage::Get(const std::string &Id) const
{
  const auto it = Definitions.find(Id);
  if (it == Definitions.end())
  {
    return nullptr;
  }
  return &it->second;
}

std::vector<std::string> UCreatureDefinitionStorage::ListAllIds() const
{
  std::vector<std::string> ids;
  ids.reserve(Definitions.size());
  for (const auto &[Id, def] : Definitions)
  {
    (void)def;
    ids.push_back(Id);
  }
  std::sort(ids.begin(), ids.end(),
            [this](const std::string &a, const std::string &b)
            {
              const auto *defA = Get(a);
              const auto *defB = Get(b);
              const int orderA = defA ? defA->catalog.sortOrder : 0;
              const int orderB = defB ? defB->catalog.sortOrder : 0;
              if (orderA != orderB)
              {
                return orderA < orderB;
              }
              return a < b;
            });
  return ids;
}

std::vector<std::string> UCreatureDefinitionStorage::ListSpawnable() const
{
  std::vector<std::string> ids;
  for (const auto &[Id, def] : Definitions)
  {
    if (def.catalog.spawnable)
    {
      ids.push_back(Id);
    }
  }
  std::sort(ids.begin(), ids.end(),
            [this](const std::string &a, const std::string &b)
            {
              const auto *defA = Get(a);
              const auto *defB = Get(b);
              const int orderA = defA ? defA->catalog.sortOrder : 0;
              const int orderB = defB ? defB->catalog.sortOrder : 0;
              if (orderA != orderB)
              {
                return orderA < orderB;
              }
              return a < b;
            });
  return ids;
}

std::string UCreatureDefinitionStorage::GetControlledDefaultSpeciesId() const
{
  for (const auto &[Id, def] : Definitions)
  {
    if (def.role == CreatureRole::ControlledDefault)
    {
      return Id;
    }
  }
  return {};
}

} // namespace cutum
