#include "App/WorldLifecycleFacade.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>

#include "App/Core.h"
#include "App/ResourcePackBootstrap.h"
#include "Creatures/Core/Creature.h"
#include "ResourcePacks/BlockMergeRegistry.h"
#include "World/Core/World.h"
#include "WorldGen/Core/ProceduralConfigIO.h"

using json = nlohmann::json;

namespace cutum
{

namespace
{

bool ParseWorldNumberSuffix(const std::string &Name, int &outNumber)
{
  constexpr const char *kPrefix = "World_";
  if (Name.size() != 9)
  {
    return false;
  }
  if (Name.compare(0, 6, kPrefix) != 0)
  {
    return false;
  }
  for (size_t i = 6; i < Name.size(); ++i)
  {
    if (!std::isdigit(static_cast<unsigned char>(Name[i])))
    {
      return false;
    }
  }
  outNumber = std::stoi(Name.substr(6));
  return true;
}

bool ResourcePackSelectionEqual(const ResourcePackSelection &a,
                                const ResourcePackSelection &b)
{
  return a.Primary == b.Primary && a.Secondary == b.Secondary &&
         a.WorldgenOwner == b.WorldgenOwner;
}

} // namespace

void UWorldLifecycleFacade::CreateWorld(UCore &core,
                                        const std::string &terrain_type)
{
  core.WorldSeed += 1;
  if (!terrain_type.empty())
  {
    core.TerrainType = terrain_type;
    core.ProceduralTemplate.Generator =
        ProceduralGeneratorFromString(terrain_type);
  }
  core.ProceduralTemplate.Seed = core.WorldSeed;
  ResetToGeneratorDefaults(core.ProceduralTemplate);
  core.TerrainType =
      ProceduralGeneratorToString(core.ProceduralTemplate.Generator);
  core.PendingNewWorldSettings.reset();
  CreateNewWorldWithCurrentSettings(core);
}

void UWorldLifecycleFacade::CreateWorldFromProceduralConfig(UCore &core)
{
  if (!core.ConfigFilePath.empty() &&
      std::filesystem::exists(core.ConfigFilePath))
  {
    std::ifstream file(core.ConfigFilePath.string());
    if (file.is_open())
    {
      std::stringstream buffer;
      buffer << file.rdbuf();
      try
      {
        const json d = json::parse(buffer.str());
        core.ProceduralTemplate = ParseProceduralTemplateFromConfig(d);
        core.WorldSeed = core.ProceduralTemplate.Seed;
      }
      catch (const json::exception &e)
      {
        std::cerr << "CreateWorldFromProceduralConfig: config parse error: "
                  << e.what() << std::endl;
      }
    }
  }

  core.WorldSeed += 1;
  core.ProceduralTemplate.Seed = core.WorldSeed;
  ResetToGeneratorDefaults(core.ProceduralTemplate);
  core.TerrainType =
      ProceduralGeneratorToString(core.ProceduralTemplate.Generator);

  std::cout << "Core::CreateWorldFromProceduralConfig: " << core.TerrainType
            << " (Seed=" << core.ProceduralTemplate.Seed << ")" << std::endl;

  core.PendingNewWorldSettings.reset();
  CreateNewWorldWithCurrentSettings(core);
}

void UWorldLifecycleFacade::CreateNewWorldWithCurrentSettings(UCore &core)
{
  const std::string new_world_name = SetupNewWorldForCreation(core);
  core.WorldInstance->Create(new_world_name);
  core.WorldInstance->GenerateUsers();
  SaveWorld(core, new_world_name);
  LoadWorldList(core, core.WorldPath.string());
}

void UWorldLifecycleFacade::LoadWorld(UCore &core, const std::string &world_name)
{
  core.ActiveWorldFolder = core.WorldFolderPath(world_name);
  core.WorldInstance->Load(core.ActiveWorldFolder.string());
  core.ApplyRuntimeStreamingToWorld();
  if (!core.DefaultUserName.empty())
  {
    if (!core.WorldInstance->SetCurrentUserName(core.DefaultUserName))
    {
      std::cerr << "Core::LoadWorld: user '" << core.DefaultUserName
                << "' not found." << std::endl;
    }
  }
  if (core.WorldInstance->GetCurrentUser() == nullptr)
  {
    core.WorldInstance->GenerateUsers();
  }
}

void UWorldLifecycleFacade::LoadLastWorld(UCore &core)
{
  if (core.DefaultWorldName.empty())
  {
    std::cerr << "Core::LoadLastWorld: default_world is not set in config."
              << std::endl;
    return;
  }

  std::cout << "Loading last World: " << core.DefaultWorldName
            << " (user: " << core.DefaultUserName << ")" << std::endl;

  LoadWorld(core, core.DefaultWorldName);

  if (!core.DefaultUserName.empty())
  {
    if (!core.WorldInstance->SetCurrentUserName(core.DefaultUserName))
    {
      std::cerr << "Core::LoadLastWorld: user '" << core.DefaultUserName
                << "' not found, using current user." << std::endl;
    }
  }

  core.WorldInstance->FinalizePlayerAfterWorldLoad();
}

void UWorldLifecycleFacade::LoadWorldByName(UCore &core,
                                            const std::string &world_name)
{
  core.DefaultWorldName = world_name;
  LoadWorld(core, world_name);
}

void UWorldLifecycleFacade::PrepareLoadWorld(UCore &core,
                                             const std::string &world_name)
{
  core.DefaultWorldName = world_name;
  core.ActiveWorldFolder = core.WorldFolderPath(world_name);
}

void UWorldLifecycleFacade::FinalizeLoadedWorld(UCore &core)
{
  core.ApplyRuntimeStreamingToWorld();
  if (!core.DefaultUserName.empty())
  {
    if (!core.WorldInstance->SetCurrentUserName(core.DefaultUserName))
    {
      std::cerr << "Core::FinalizeLoadedWorld: user '" << core.DefaultUserName
                << "' not found." << std::endl;
    }
  }
  if (core.WorldInstance->GetCurrentUser() == nullptr)
  {
    core.WorldInstance->GenerateUsers();
  }
}

void UWorldLifecycleFacade::FinalizeEnterGameSession(UCore &core)
{
  if (core.DefaultUserName.empty())
  {
    core.DefaultUserName = core.WorldInstance->GetCurrentUserName();
  }
  if (core.WorldInstance->GetCurrentUser() == nullptr)
  {
    core.WorldInstance->GenerateUsers();
  }
  if (UCreature *player = core.WorldInstance->GetPlayerCreature())
  {
    if (player->GetInventory().GetActiveEntryRef() == nullptr)
    {
      player->GetInventory().SetActiveSlot(0, 1);
    }
  }
}

void UWorldLifecycleFacade::PrepareEnterGameWorldList(UCore &core)
{
  std::filesystem::create_directories(core.WorldPath);
  LoadWorldList(core, core.WorldPath.string());
}

void UWorldLifecycleFacade::LoadWorldList(UCore &core,
                                          const std::string &world_path)
{
  core.WorldList.clear();

  if (!std::filesystem::exists(world_path) ||
      !std::filesystem::is_directory(world_path))
  {
    return;
  }

  try
  {
    for (const auto &entry : std::filesystem::directory_iterator(world_path))
    {
      if (!entry.is_directory())
      {
        continue;
      }
      const std::string Name = entry.path().filename().string();
      if (std::find(core.WorldList.begin(), core.WorldList.end(), Name) ==
          core.WorldList.end())
      {
        core.WorldList.push_back(Name);
      }
    }
  }
  catch (const std::filesystem::filesystem_error &ex)
  {
    std::cerr << ex.what() << std::endl;
  }
}

void UWorldLifecycleFacade::RefreshWorldList(UCore &core)
{
  std::filesystem::create_directories(core.WorldPath);
  LoadWorldList(core, core.WorldPath.string());
}

void UWorldLifecycleFacade::SaveWorld(UCore &core, const std::string &world_name)
{
  if (core.ActiveWorldFolder.empty())
  {
    core.ActiveWorldFolder = core.WorldFolderPath(world_name);
  }
  if (core.WorldInstance && core.BlockMergeRegistryInstance)
  {
    core.WorldInstance->SetCatalogFingerprint(
        core.BlockMergeRegistryInstance->ComputeCatalogFingerprint());
  }
  core.WorldInstance->Save(core.ActiveWorldFolder.string());
}

void UWorldLifecycleFacade::CreateNewWorldWithSettings(
    UCore &core, const ProceduralSettings &settings,
    const std::vector<std::string> &resourcePacks)
{
  ResourcePackSelection selection;
  selection.Primary = resourcePacks;
  if (!selection.Primary.empty())
  {
    selection.WorldgenOwner = selection.Primary.front();
  }
  CreateNewWorldWithSettings(core, settings, selection);
}

void UWorldLifecycleFacade::CreateNewWorldWithSettings(
    UCore &core, const ProceduralSettings &settings,
    const ResourcePackSelection &resourcePacks)
{
  ApplyNewWorldCreationRequest(core, settings, resourcePacks);
  CreateNewWorldWithCurrentSettings(core);
}

void UWorldLifecycleFacade::ApplyNewWorldCreationRequest(
    UCore &core, const ProceduralSettings &settings,
    const ResourcePackSelection &resourcePacks)
{
  core.PendingNewWorldSettings = settings;
  core.PendingNewWorldSettings->Seed = settings.Seed;
  core.WorldSeed = settings.Seed + 1;
  core.PendingNewWorldSettings->Seed = core.WorldSeed;

  core.ProceduralTemplate.Generator = settings.Generator;
  core.ProceduralTemplate.Seed = core.WorldSeed;
  ResetToGeneratorDefaults(core.ProceduralTemplate);
  core.TerrainType =
      ProceduralGeneratorToString(core.ProceduralTemplate.Generator);

  core.PendingNewWorldPackSelection = resourcePacks;
}

std::string UWorldLifecycleFacade::AllocateNextWorldName(const UCore &core) const
{
  int maxNumber = 0;
  if (std::filesystem::exists(core.WorldPath) &&
      std::filesystem::is_directory(core.WorldPath))
  {
    for (const auto &entry : std::filesystem::directory_iterator(core.WorldPath))
    {
      if (!entry.is_directory())
      {
        continue;
      }
      int number = 0;
      if (ParseWorldNumberSuffix(entry.path().filename().string(), number))
      {
        maxNumber = std::max(maxNumber, number);
      }
    }
  }

  char nameBuffer[16];
  std::snprintf(nameBuffer, sizeof(nameBuffer), "World_%03d", maxNumber + 1);
  return nameBuffer;
}

std::string UWorldLifecycleFacade::SetupNewWorldForCreation(UCore &core)
{
  ResourcePackSelection selection = core.PendingNewWorldPackSelection;
  const std::vector<std::string> legacyPacks = core.PendingNewWorldResourcePacks;
  core.PendingNewWorldPackSelection = {};
  core.PendingNewWorldResourcePacks.clear();
  if (selection.Primary.empty())
  {
    selection.Primary =
        core.ResourcePackBootstrap.NormalizeEnabledPackIds(core, legacyPacks);
  }
  if (selection.Primary.empty())
  {
    selection = core.GetDefaultResourcePackSelection();
  }
  selection.Primary =
      core.ResourcePackBootstrap.NormalizeEnabledPackIds(core, selection.Primary);
  selection.Secondary =
      core.ResourcePackBootstrap.NormalizeEnabledPackIds(core, selection.Secondary);
  if (selection.Primary.empty())
  {
    selection = core.GetDefaultResourcePackSelection();
    selection.Primary =
        core.ResourcePackBootstrap.NormalizeEnabledPackIds(core, selection.Primary);
    selection.Secondary =
        core.ResourcePackBootstrap.NormalizeEnabledPackIds(core, selection.Secondary);
  }
  if (selection.WorldgenOwner.empty() && !selection.Primary.empty())
  {
    selection.WorldgenOwner = selection.Primary.front();
  }

  core.WorldInstance->SetResourcePackSelection(
      selection.Primary, selection.Secondary, selection.WorldgenOwner);
  if (!ResourcePackSelectionEqual(selection, core.ActivePackSelection))
  {
    core.ResourcePackBootstrap.ApplyResourcePacks(core, selection);
  }

  const std::string new_world_name = AllocateNextWorldName(core);
  core.DefaultWorldName = new_world_name;
  core.ActiveWorldFolder = core.WorldFolderPath(new_world_name);
  std::filesystem::create_directories(core.ActiveWorldFolder / "chunks");

  std::cout << "Core::CreateWorld: new world '" << new_world_name << "' at "
            << core.ActiveWorldFolder.string() << std::endl;

  ProceduralSettings worldSettings = core.ProceduralTemplate;
  if (core.PendingNewWorldSettings.has_value())
  {
    worldSettings = *core.PendingNewWorldSettings;
    core.PendingNewWorldSettings.reset();
  }
  else
  {
    worldSettings.Seed = core.WorldSeed;
    ResetToGeneratorDefaults(worldSettings);
  }
  ResolveProceduralDefaults(worldSettings);
  ApplyGeneratorTierDefaults(worldSettings);
  worldSettings.AsyncChunkGeneration =
      core.ProceduralTemplate.AsyncChunkGeneration;
  worldSettings.AsyncChunkIo = core.ProceduralTemplate.AsyncChunkIo;
  worldSettings.MaxChunkCommitsPerFrame =
      core.ProceduralTemplate.MaxChunkCommitsPerFrame;

  core.WorldInstance->SetProceduralSettings(worldSettings);
  core.WorldInstance->SetRenderSettings(core.Render);
  return new_world_name;
}

void UWorldLifecycleFacade::RefreshWorldListAfterSave(UCore &core)
{
  LoadWorldList(core, core.WorldPath.string());
}

bool UWorldLifecycleFacade::CreateWorldHeadless(UCore &core,
                                                const CreateWorldCliArgs &args,
                                                CreateWorldReport &report)
{
  report = CreateWorldReport{};
  report.WorldName = args.WorldName;
  report.Seed = args.Seed;
  report.Generator = ProceduralGeneratorToString(args.Generator);
  report.Preset = args.Preset;
  report.RadiusChunks = args.RadiusChunks;

  if (!core.WorldInstance)
  {
    report.Error = "World instance is not initialized.";
    return false;
  }

  ResourcePackSelection selection = core.GetDefaultResourcePackSelection();
  if (!args.PrimaryPacks.empty())
  {
    selection.Primary =
        core.ResourcePackBootstrap.NormalizeEnabledPackIds(core, args.PrimaryPacks);
    selection.WorldgenOwner = args.WorldgenOwnerPack.empty()
                                  ? selection.Primary.front()
                                  : args.WorldgenOwnerPack;
  }
  selection.Primary =
      core.ResourcePackBootstrap.NormalizeEnabledPackIds(core, selection.Primary);
  selection.Secondary =
      core.ResourcePackBootstrap.NormalizeEnabledPackIds(core, selection.Secondary);
  if (!core.ResourcePackBootstrap.ApplyResourcePacks(core, selection))
  {
    report.Error = "Failed to apply resource packs.";
    return false;
  }

  ProceduralSettings settings = core.ProceduralTemplate;
  settings.Generator = args.Generator;
  settings.Seed = args.Seed;
  ResetToGeneratorDefaults(settings);
  settings.Generator = args.Generator;
  settings.Seed = args.Seed;
  ApplyWorldGenPreset(settings, args.Preset);
  settings.AsyncChunkGeneration = false;
  settings.AsyncChunkIo = false;
  ResolveProceduralDefaults(settings);
  ApplyGeneratorTierDefaults(settings);

  core.RenderDistanceChunks = std::max(1, args.RadiusChunks);
  core.WorldInstance->SetRenderDistanceChunks(core.RenderDistanceChunks);
  core.WorldInstance->SetStreamingEnabled(false);

  const std::filesystem::path output_root = args.OutputRoot.is_absolute()
                                                ? args.OutputRoot
                                                : (core.ExeDir / args.OutputRoot);
  std::error_code ec;
  std::filesystem::create_directories(output_root, ec);
  core.ActiveWorldFolder = output_root / args.WorldName;
  std::filesystem::create_directories(core.ActiveWorldFolder, ec);
  std::filesystem::create_directories(core.ActiveWorldFolder / "chunks", ec);
  core.DefaultWorldName = args.WorldName;
  core.WorldPath = output_root;

  core.WorldInstance->SetResourcePackSelection(
      selection.Primary, selection.Secondary, selection.WorldgenOwner);
  core.WorldInstance->SetProceduralSettings(settings);
  core.WorldInstance->SetRenderSettings(core.Render);

  try
  {
    core.WorldInstance->Create(args.WorldName);
    core.WorldInstance->GenerateUsers();
    if (core.BlockMergeRegistryInstance)
    {
      core.WorldInstance->SetCatalogFingerprint(
          core.BlockMergeRegistryInstance->ComputeCatalogFingerprint());
    }
    core.WorldInstance->Save(core.ActiveWorldFolder.string());
  }
  catch (const std::exception &e)
  {
    report.Error = e.what();
    return false;
  }

  const std::filesystem::path chunks_dir = core.ActiveWorldFolder / "chunks";
  int chunk_files = 0;
  if (std::filesystem::exists(chunks_dir))
  {
    for (const auto &entry : std::filesystem::directory_iterator(chunks_dir))
    {
      if (entry.path().extension() == ".cchunk")
      {
        ++chunk_files;
      }
    }
  }

  report.Success = true;
  report.WorldPath = core.ActiveWorldFolder.string();
  report.ChunkFiles = chunk_files;
  report.SpawnY = core.WorldInstance->GetSpawnPoint().y;
  return true;
}

} // namespace cutum
