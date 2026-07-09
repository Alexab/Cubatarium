#include "World/Persistence/WorldPersistence.h"
#include "Blocks/BlockRegistry.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureInventory.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Player/Player.h"
#include "Creatures/Player/PlayerCapsule.h"
#include "Creatures/Player/User.h"
#include "Creatures/Visual/CreaturePartMeshData.h"
#include "Creatures/Visual/CreatureVisualFactory.h"
#include "Render/Camera/Camera.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkBuffer.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Chunks/ChunkStreamer.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/World.h"
#include "World/Environment/EnvironmentConfig.h"
#include "World/Math/GridMath.h"
#include "World/Streaming/WorldStreaming.h"
#include "WorldGen/Core/ProceduralConfigIO.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenSets.h"
#include "WorldGen/Features/ObjectFeatureConfig.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>

using json = nlohmann::json;

namespace cutum
{

namespace
{

constexpr float kMaxReasonablePlayerY = 512.0f;
constexpr float kMinReasonablePlayerY = -32.0f;

bool HasChunkDataFiles(const std::string &chunks_dir)
{
  if (!std::filesystem::exists(chunks_dir) ||
      !std::filesystem::is_directory(chunks_dir))
  {
    return false;
  }
  for (const auto &entry : std::filesystem::directory_iterator(chunks_dir))
  {
    const auto ext = entry.path().extension();
    if (ext == ".json" || ext == ".cchunk")
    {
      return true;
    }
  }
  return false;
}

} // namespace

UWorldPersistence::UWorldPersistence()
{
  ChunkStorage = std::make_unique<UChunkStorageService>();
}

bool UWorldPersistence::HasPersistedTerrainOnDisk(
    const std::string &world_folder_path)
{
  const std::string chunks_dir = world_folder_path + "/chunks";
  if (HasChunkDataFiles(chunks_dir) ||
      UChunkStorageService::HasChunkFilesOnDisk(world_folder_path))
  {
    return true;
  }

  const std::string chunks_file = world_folder_path + "/chunks.json";
  if (!std::filesystem::exists(chunks_file))
  {
    return false;
  }

  try
  {
    std::ifstream file(chunks_file);
    if (!file.is_open())
    {
      return false;
    }
    const json data = json::parse(file);
    const std::string storage = data.value("storage", "");
    return storage == "per_file" || storage == "binary" || storage == "json";
  }
  catch (const json::exception &)
  {
    return false;
  }
}

void UWorldPersistence::EnsureChunkIoInitialized()
{
  if (!AsyncChunkIo)
  {
    AsyncChunkIo = std::make_unique<UAsyncChunkIO>();
  }
  if (!ChunkStorage)
  {
    ChunkStorage = std::make_unique<UChunkStorageService>();
  }
}

void UWorldPersistence::SetChunkWriteFormat(ChunkWriteFormat format)
{
  if (!ChunkStorage)
  {
    ChunkStorage = std::make_unique<UChunkStorageService>();
  }
  ChunkStorage->SetWriteFormat(format);
}

ChunkWriteFormat UWorldPersistence::GetChunkWriteFormat() const
{
  return ChunkStorage ? ChunkStorage->GetSettings().writeFormat
                      : ChunkWriteFormat::Binary;
}

void UWorldPersistence::TickAsyncChunkIo(UWorld &world)
{
  if (!ChunkStorage)
  {
    return;
  }

  if (AsyncChunkIo && world.ProceduralTemplate.AsyncChunkIo)
  {
    for (AsyncChunkLoadResult &load : AsyncChunkIo->DrainLoads())
    {
      const glm::ivec3 ground(load.coord.x, 0, load.coord.z);
      bool column_completed = false;
      const auto pending_it = PendingAsyncColumnLoadSlices.find(ground);
      if (pending_it != PendingAsyncColumnLoadSlices.end())
      {
        const int remaining = --pending_it->second;
        if (remaining <= 0)
        {
          PendingAsyncColumnLoadSlices.erase(pending_it);
          column_completed = true;
        }
      }
      else
      {
        column_completed = true;
      }

      if (load.success &&
          load.token.IsValidFor(load.coord, load.token.sequence) &&
          world.BlockRegistry)
      {
        const UChunkBuffer buffer = ChunkStorage->DeserializeChunk(
            load.payload, load.coord, load.format, *world.BlockRegistry);
        if (!buffer.IsEmpty())
        {
          buffer.ApplyTo(world.BlockWorld);
        }
      }

      if (column_completed && world.Streaming && world.Streaming->GetStreamer())
      {
        if (load.success && world.BlockRegistry)
        {
          const int max_y = world.ProceduralTemplate.MaxHeight;
          world.RelightTerrainColumn(ground.x, ground.z, 0, max_y);
        }
        world.Streaming->GetStreamer()->NotifyChunkCommitted(ground);
      }
    }

    for (AsyncChunkSaveRequest &save : AsyncChunkIo->DrainSaves())
    {
      if (ChunkStorage->GetSettings().writeFormat == ChunkWriteFormat::Binary &&
          ChunkStorage->GetSettings().deleteLegacyJsonOnBinarySave)
      {
        const std::string legacy_json = ChunkStorage->ChunkFilePath(
            WorldFolderPath, save.coord, ChunkDiskFormat::Json);
        std::error_code ec;
        std::filesystem::remove(legacy_json, ec);
      }
      auto pending_it = PendingAsyncColumnSaveSlices.find(save.groundCoord);
      if (pending_it != PendingAsyncColumnSaveSlices.end())
      {
        --pending_it->second;
        if (pending_it->second <= 0)
        {
          PendingAsyncColumnSaveSlices.erase(pending_it);
          ChunkStorage->ClearColumnSavePending(save.groundCoord);
        }
      }
      else
      {
        ChunkStorage->ClearColumnSavePending(save.groundCoord);
      }
    }
  }
}

void UWorldPersistence::RequestAsyncTerrainColumnLoad(UWorld &world,
                                                      glm::ivec3 ground_coord)
{
  if (!AsyncChunkIo || !ChunkStorage || !world.BlockRegistry)
  {
    return;
  }
  if (ground_coord.y != 0)
  {
    ground_coord.y = 0;
  }
  if (ChunkStorage->IsColumnSavePending(ground_coord) ||
      PendingAsyncColumnLoadSlices.count(ground_coord) > 0)
  {
    return;
  }
  const int max_cy =
      (world.ProceduralTemplate.MaxHeight + CHUNK_SIZE - 1) / CHUNK_SIZE;
  const int slice_count = max_cy + 1;
  PendingAsyncColumnLoadSlices[ground_coord] = slice_count;
  for (int cy = 0; cy <= max_cy; ++cy)
  {
    const glm::ivec3 slice(ground_coord.x, cy, ground_coord.z);
    AsyncChunkIo->RequestLoad(
        slice, *ChunkStorage, WorldFolderPath,
        world.Streaming->GetChunkGenTokens().Current(ground_coord));
  }
}

void UWorldPersistence::RequestAsyncTerrainColumnSave(UWorld &world,
                                                      glm::ivec3 ground_coord)
{
  if (!AsyncChunkIo || !ChunkStorage || !world.BlockRegistry)
  {
    return;
  }
  if (ground_coord.y != 0)
  {
    ground_coord.y = 0;
  }
  if (ChunkStorage->IsColumnSavePending(ground_coord) ||
      PendingAsyncColumnSaveSlices.count(ground_coord) > 0)
  {
    return;
  }
  const int max_cy =
      (world.ProceduralTemplate.MaxHeight + CHUNK_SIZE - 1) / CHUNK_SIZE;
  int save_count = 0;
  for (int cy = 0; cy <= max_cy; ++cy)
  {
    const glm::ivec3 slice(ground_coord.x, cy, ground_coord.z);
    if (!world.BlockWorld.GetChunkManager().HasChunk(slice))
    {
      continue;
    }
    ++save_count;
  }
  if (save_count == 0)
  {
    return;
  }
  ChunkStorage->MarkColumnSavePending(ground_coord);
  PendingAsyncColumnSaveSlices[ground_coord] = save_count;
  for (int cy = 0; cy <= max_cy; ++cy)
  {
    const glm::ivec3 slice(ground_coord.x, cy, ground_coord.z);
    if (!world.BlockWorld.GetChunkManager().HasChunk(slice))
    {
      continue;
    }
    AsyncChunkIo->RequestSave(
        slice, *ChunkStorage, WorldFolderPath, world.BlockWorld,
        *world.BlockRegistry,
        world.Streaming->GetChunkGenTokens().Current(ground_coord));
  }
}

bool UWorldPersistence::IsTerrainColumnDiskLoadPending(
    glm::ivec3 ground_coord) const
{
  if (ground_coord.y != 0)
  {
    ground_coord.y = 0;
  }
  return PendingAsyncColumnLoadSlices.count(ground_coord) > 0;
}

int UWorldPersistence::LoadTerrainColumn(glm::ivec3 coord,
                                         UBlockWorld &block_world,
                                         UBlockRegistry &registry,
                                         int max_height)
{
  if (!ChunkStorage)
  {
    return 0;
  }
  return ChunkStorage->LoadTerrainColumn(coord, block_world, WorldFolderPath,
                                         registry, max_height);
}

void UWorldPersistence::SaveTerrainColumn(glm::ivec3 ground_coord,
                                          UBlockWorld &block_world,
                                          UBlockRegistry &registry,
                                          int max_height)
{
  if (!ChunkStorage)
  {
    return;
  }
  ChunkStorage->SaveTerrainColumn(ground_coord, block_world, WorldFolderPath,
                                  registry, max_height);
}

void UWorldPersistence::LoadInitialTerrainColumns(UWorld &world,
                                                  glm::vec3 spawn_point,
                                                  int render_distance_chunks)
{
  if (!ChunkStorage || !world.BlockRegistry)
  {
    return;
  }
  const glm::ivec3 spawn_block = WorldPosToBlock(spawn_point);
  const glm::ivec3 center_chunk = UChunkManager::WorldToChunk(spawn_block);
  const int radius = render_distance_chunks + 1;
  for (int dx = -radius; dx <= radius; ++dx)
  {
    for (int dz = -radius; dz <= radius; ++dz)
    {
      LoadTerrainColumn(glm::ivec3(center_chunk.x + dx, 0, center_chunk.z + dz),
                        world.BlockWorld, *world.BlockRegistry,
                        world.ProceduralTemplate.MaxHeight);
    }
  }
}

void UWorldPersistence::LoadUsers(UWorld &world, const std::string &file_name)
{
  std::string val;
  std::ifstream file(file_name);
  if (file.is_open())
  {
    std::stringstream buffer;
    buffer << file.rdbuf();
    val = buffer.str();
    file.close();
  }
  else
  {
    std::cerr << "Failed to open users file: " << file_name << std::endl;
    return;
  }

  try
  {
    world.Users.clear();
    json d = json::parse(val);
    for (auto i = d.begin(); i != d.end(); ++i)
    {
      const auto user_name = i.key();
      const auto user_data = i.value();

      world.AddUser(user_name);
      auto user = world.GetUser(user_name);
      if (!user)
      {
        continue;
      }

      glm::vec3 position = world.SpawnPoint;
      const auto position_value = user_data.value("position", json::array());
      if (position_value.is_array() && position_value.size() == 3)
      {
        position = glm::vec3(position_value[0].get<float>(),
                             position_value[1].get<float>(),
                             position_value[2].get<float>());
      }
      user->SetPosition(position);
      world.SanitizeUserPosition(user);

      if (user_data.contains("player_creature_id"))
      {
        const CreatureId saved_id =
            user_data["player_creature_id"].get<CreatureId>();
        if (world.GetCreature(saved_id))
        {
          user->SetPlayerCreatureId(saved_id);
          world.Environment.SetPlayerCreatureId(saved_id);
        }
      }
      if (user_data.contains("selected_skin_id"))
      {
        user->SetSelectedSkinId(
            user_data["selected_skin_id"].get<std::string>());
      }
      else if (user_data.contains("selected_appearance_type"))
      {
        user->SetSelectedAppearanceTypeId(
            user_data["selected_appearance_type"].get<std::string>());
        user->SetSelectedSkinId(
            user_data["selected_appearance_type"].get<std::string>());
      }
      UCreature *player_creature =
          world.GetCreature(user->GetPlayerCreatureId());
      if (!player_creature && world.Environment.GetPlayerCreatureId() != 0)
      {
        user->SetPlayerCreatureId(world.Environment.GetPlayerCreatureId());
        player_creature =
            world.GetCreature(world.Environment.GetPlayerCreatureId());
      }
      if (!player_creature)
      {
        std::string species_id = "human";
        if (const auto &creature_definitions =
                world.GetCreatureDefinitionStorage())
        {
          const std::string controlled =
              creature_definitions->GetControlledDefaultSpeciesId();
          if (!controlled.empty())
          {
            species_id = controlled;
          }
        }
        const glm::vec3 eye_offset(0.0f, 1.62f, 0.0f);
        const glm::vec3 body_origin = BodyOriginFromEye(position, eye_offset);
        const CreatureId pid = world.SpawnCreature(species_id, body_origin);
        if (pid != 0)
        {
          user->SetPlayerCreatureId(pid);
          world.Environment.SetPlayerCreatureId(pid);
          if (world.Users.size() == 1)
          {
            world.Environment.SetControlledCreatureId(pid);
          }
          if (UPlayer *player = dynamic_cast<UPlayer *>(world.GetCreature(pid)))
          {
            player->BindUser(user);
          }
          player_creature = world.GetCreature(pid);
        }
      }
      if (player_creature)
      {
        const glm::vec3 eye_offset = player_creature->GetEyeOffset();
        player_creature->SetBodyOrigin(
            BodyOriginFromEye(user->GetPosition(), eye_offset));
      }

      float yaw = -90.0f;
      float pitch = 0.0f;
      if (user_data.contains("yaw"))
      {
        yaw = user_data["yaw"].get<float>();
      }
      if (user_data.contains("pitch"))
      {
        pitch = user_data["pitch"].get<float>();
      }
      user->SetCameraOrientation(yaw, pitch);

      const size_t hotbar_count = 2;
      if (player_creature)
      {
        UCreatureInventory &inv = player_creature->GetInventory();
        const bool had_hotbars =
            user_data.contains("hotbars") && user_data["hotbars"].is_array();
        inv.DeserializeFromJson(user_data, hotbar_count);
        if (inv.GetStorage().empty())
        {
          inv.InitCreativeDefaults();
        }
        if (!had_hotbars || inv.IsPrimaryHotbarEmpty())
        {
          inv.EnsureDefaultHotbar();
        }
        player_creature->SetOrientation(ModelYawFromCameraYaw(yaw), pitch);
        if (!user->GetSelectedSkinId().empty())
        {
          player_creature->SetSkinId(user->GetSelectedSkinId());
          if (const CreatureDefinition *def =
                  world.GetCreatureDefinition(player_creature->GetTypeId()))
          {
            player_creature->SetVisual(CreateCreatureVisual(*def));
          }
        }
      }

      if (auto camera = world.GetUserCamera(user_name))
      {
        camera->SetPosition(position);
        camera->SetOrientation(yaw, pitch);
      }
    }
  }
  catch (const json::exception &e)
  {
    std::cerr << "JSON parsing error in LoadUsers: " << e.what() << std::endl;
  }
}

void UWorldPersistence::SaveUsers(UWorld &world, const std::string &file_name)
{
  json objects;

  for (auto i = world.Users.begin(); i != world.Users.end(); ++i)
  {
    const auto &user_name = i->first;
    auto user = i->second;

    glm::vec3 position = user->GetPosition();
    float yaw = user->GetCameraYaw();
    float pitch = user->GetCameraPitch();
    if (user_name == world.CurrentUserName)
    {
      if (auto camera = world.GetUserCamera(user_name))
      {
        position = camera->GetPosition();
        yaw = camera->GetYaw();
        pitch = camera->GetPitch();
        user->SetPosition(position);
        user->SetCameraOrientation(yaw, pitch);
      }
    }

    json user_json;
    user_json["position"] = json::array({position.x, position.y, position.z});
    user_json["yaw"] = yaw;
    user_json["pitch"] = pitch;
    user_json["player_creature_id"] = user->GetPlayerCreatureId();
    if (!user->GetSelectedSkinId().empty())
    {
      user_json["selected_skin_id"] = user->GetSelectedSkinId();
    }
    else if (!user->GetSelectedAppearanceTypeId().empty())
    {
      user_json["selected_appearance_type"] =
          user->GetSelectedAppearanceTypeId();
    }

    if (UCreature *player_creature =
            world.GetCreature(user->GetPlayerCreatureId()))
    {
      player_creature->GetInventory().SerializeToJson(user_json);
    }

    objects[user_name] = user_json;
  }

  std::ofstream file(file_name);
  if (file.is_open())
  {
    file << objects.dump(4);
    file.close();
  }
}

void UWorldPersistence::LoadWorldData(UWorld &world,
                                      const std::string &file_name)
{
  std::string val;
  std::ifstream file(file_name);
  if (file.is_open())
  {
    std::stringstream buffer;
    buffer << file.rdbuf();
    val = buffer.str();
    file.close();
  }
  else
  {
    std::cerr << "Failed to open world data file: " << file_name << std::endl;
    return;
  }

  try
  {
    json d = json::parse(val);
    std::string world_name_value = d.value("world_name", "");
    json spawn_point_value = d.value("spawn_point", json::array());

    if (world_name_value.empty() || spawn_point_value.empty())
    {
      return;
    }

    if (!spawn_point_value.is_array())
    {
      return;
    }

    if (spawn_point_value.size() != 3)
    {
      return;
    }

    glm::vec3 spawn_point(spawn_point_value[0].get<float>(),
                          spawn_point_value[1].get<float>(),
                          spawn_point_value[2].get<float>());

    world.WorldName = world_name_value;
    world.SpawnPoint = spawn_point;

    if (d.contains("terrain") && d["terrain"].is_string())
    {
      world.TerrainType = d["terrain"].get<std::string>();
    }
    if (d.contains("world_seed"))
    {
      world.WorldSeed = d["world_seed"].get<uint32_t>();
    }
    if (d.contains("procedural") && d["procedural"].is_object())
    {
      world.ProceduralTemplate = ParseProceduralSettings(d);
      world.TerrainType =
          ProceduralGeneratorToString(world.ProceduralTemplate.Generator);
      world.WorldSeed = world.ProceduralTemplate.Seed;
    }
    else
    {
      world.ProceduralTemplate.Seed = world.WorldSeed;
      world.ProceduralTemplate.Generator =
          ProceduralGeneratorFromString(world.TerrainType);
      ResolveProceduralDefaults(world.ProceduralTemplate);
      ApplyGeneratorTierDefaults(world.ProceduralTemplate);
    }
    world.ResourcePacksEnabled.clear();
    world.ResourcePacksPrimary.clear();
    world.ResourcePacksSecondary.clear();
    world.WorldgenOwnerPackId.clear();
    if (d.contains("resource_packs") && d["resource_packs"].is_object())
    {
      const auto &rp = d["resource_packs"];
      auto parse_ids =
          [](const nlohmann::json &arr, std::vector<std::string> &out)
      {
        if (!arr.is_array())
        {
          return;
        }
        out.reserve(arr.size());
        for (const auto &id : arr)
        {
          if (id.is_string())
          {
            out.push_back(id.get<std::string>());
          }
        }
      };
      if (rp.contains("primary") && rp["primary"].is_array())
      {
        parse_ids(rp["primary"], world.ResourcePacksPrimary);
      }
      if (rp.contains("secondary") && rp["secondary"].is_array())
      {
        parse_ids(rp["secondary"], world.ResourcePacksSecondary);
      }
      if (world.ResourcePacksPrimary.empty() && rp.contains("enabled") &&
          rp["enabled"].is_array())
      {
        parse_ids(rp["enabled"], world.ResourcePacksPrimary);
      }
      if (rp.contains("worldgen_owner") && rp["worldgen_owner"].is_string())
      {
        world.WorldgenOwnerPackId = rp["worldgen_owner"].get<std::string>();
      }
      world.ResourcePacksEnabled = world.ResourcePacksPrimary;
      world.ResourcePacksEnabled.insert(world.ResourcePacksEnabled.end(),
                                        world.ResourcePacksSecondary.begin(),
                                        world.ResourcePacksSecondary.end());
    }
    if (d.contains("catalog_fingerprint") &&
        d["catalog_fingerprint"].is_string())
    {
      world.CatalogFingerprint = d["catalog_fingerprint"].get<std::string>();
    }
    else
    {
      world.CatalogFingerprint.clear();
    }
    if (d.contains("worldgen_sets") && d["worldgen_sets"].is_object())
    {
      std::string wg_error;
      if (!ParseWorldGenSets(d["worldgen_sets"], world.WorldGenSetsData,
                             wg_error))
      {
        std::cerr << "LoadWorldData: worldgen_sets error: " << wg_error
                  << std::endl;
      }
      else
      {
        std::string val_error;
        if (!ValidateWorldGenSets(world.WorldGenSetsData, val_error))
        {
          std::cerr << "LoadWorldData: worldgen_sets validation: " << val_error
                    << std::endl;
        }
        world.RebuildResolvedObjectFeatures();
      }
    }
    else
    {
      std::cerr << "LoadWorldData: missing required worldgen_sets" << std::endl;
    }

    if (d.contains("environment") && d["environment"].is_object())
    {
      const json &env = d["environment"];
      EnvironmentConfig config = EnvironmentConfig::FromJson(env);
      world.ApplyEnvironmentConfig(config, false);
      world.SetTimeFrozen(env.value("time_frozen", false));
      if (env.contains("weather") && env["weather"].is_string())
      {
        UWorld::WeatherType weather = UWorld::WeatherType::Clear;
        if (UWorld::WeatherTypeFromString(env["weather"].get<std::string>(),
                                          weather))
        {
          world.SetWeatherInternal(weather, config.WeatherAuto.TransitionSeconds,
                                   config.WeatherRuntime.ManualOverride);
        }
      }
      if (env.contains("weather_target") && env["weather_target"].is_string())
      {
        UWorld::WeatherType target = UWorld::WeatherType::Clear;
        if (UWorld::WeatherTypeFromString(
                env["weather_target"].get<std::string>(), target))
        {
          world.SetWeatherInternal(target, config.WeatherAuto.TransitionSeconds,
                                   config.WeatherRuntime.ManualOverride);
        }
      }
      if (env.contains("lighting") && env["lighting"].is_object())
      {
        const json &lighting = env["lighting"];
        world.SetLightingDebugEnabled(lighting.value("debug", false));
        world.SetWeatherOverlayEnabled(lighting.value("weather_overlay", true));
        world.SetWeatherParticlesEnabled(
            lighting.value("weather_particles", true));
      }
      if (env.contains("star_visibility"))
      {
        world.EnvironmentStateData.StarVisibility =
            std::clamp(env.value("star_visibility", 0.0f), 0.0f, 1.0f);
      }
      if (env.contains("cloud_coverage"))
      {
        world.EnvironmentStateData.CloudCoverage =
            std::clamp(env.value("cloud_coverage", 0.2f), 0.0f, 1.0f);
      }
      if (env.contains("star_visibility_override"))
      {
        world.EnvironmentStateData.StarVisibilityOverride = std::clamp(
            env.value("star_visibility_override", -1.0f), -1.0f, 1.0f);
      }
      if (env.contains("cloud_coverage_override"))
      {
        world.EnvironmentStateData.CloudCoverageOverride = std::clamp(
            env.value("cloud_coverage_override", -1.0f), -1.0f, 1.0f);
      }
      world.EnsureDefaultCelestialBodies();
    }
  }
  catch (const json::exception &e)
  {
    std::cerr << "JSON parsing error in LoadWorldData: " << e.what()
              << std::endl;
  }
}

void UWorldPersistence::SaveWorldData(UWorld &world,
                                      const std::string &file_name)
{
  json world_data;

  world_data["world_name"] = world.WorldName;
  world_data["terrain"] = world.TerrainType;
  world_data["world_seed"] = world.WorldSeed;
  WriteProceduralSettings(world_data, world.ProceduralTemplate);

  json arr =
      json::array({world.SpawnPoint.x, world.SpawnPoint.y, world.SpawnPoint.z});
  world_data["spawn_point"] = arr;

  if (!world.ResourcePacksPrimary.empty() ||
      !world.ResourcePacksSecondary.empty())
  {
    auto &rp = world_data["resource_packs"];
    if (!world.ResourcePacksPrimary.empty())
    {
      rp["primary"] = world.ResourcePacksPrimary;
    }
    if (!world.ResourcePacksSecondary.empty())
    {
      rp["secondary"] = world.ResourcePacksSecondary;
    }
    if (!world.WorldgenOwnerPackId.empty())
    {
      rp["worldgen_owner"] = world.WorldgenOwnerPackId;
    }
  }
  else if (!world.ResourcePacksEnabled.empty())
  {
    world_data["resource_packs"]["primary"] = world.ResourcePacksEnabled;
  }

  if (!world.CatalogFingerprint.empty())
  {
    world_data["catalog_fingerprint"] = world.CatalogFingerprint;
  }
  WriteWorldGenSets(world_data, world.WorldGenSetsData);
  world.SyncDefaultCelestialBodiesToConfig();
  EnvironmentConfig config = world.GetEnvironmentConfig();
  config.TimeOfDay = world.GetEnvironmentState().TimeOfDayNormalized;
  config.DayLengthMinutes = world.GetEnvironmentState().DayLengthMinutes;
  json env = config.ToJson();
  env["time_frozen"] = world.GetEnvironmentState().TimeFrozen;
  env["weather"] =
      UWorld::WeatherTypeToString(world.GetEnvironmentState().Weather);
  env["weather_target"] =
      UWorld::WeatherTypeToString(world.GetEnvironmentState().TargetWeather);
  json weather_auto;
  config.WriteWeatherAutoToJson(weather_auto);
  env["weather_auto"] = weather_auto;
  env["lighting_version"] = 1;
  env["lighting"]["debug"] = world.GetLightingSettings().DebugEnabled;
  env["lighting"]["weather_overlay"] =
      world.GetLightingSettings().WeatherOverlayEnabled;
  env["lighting"]["weather_particles"] =
      world.GetLightingSettings().WeatherParticlesEnabled;
  env["star_visibility"] = world.GetEnvironmentState().StarVisibility;
  env["cloud_coverage"] = world.GetEnvironmentState().CloudCoverage;
  env["star_visibility_override"] =
      world.GetEnvironmentState().StarVisibilityOverride;
  env["cloud_coverage_override"] =
      world.GetEnvironmentState().CloudCoverageOverride;
  world_data["environment"] = env;

  std::ofstream file(file_name);
  if (file.is_open())
  {
    file << world_data.dump(4);
    file.close();
  }
}

} // namespace cutum
