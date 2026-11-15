#pragma once
#include "App/Platform/IUPlatformPaths.h"
#include "Blocks/BlockRegistry.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Visual/CreatureRenderStats.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ItemGltfTextureCache.h"
#include "Items/ItemRenderAsset.h"
#include "Render/Camera/Camera.h"
#include "Render/GlIncludes.h"
#include "World/Core/World.h"
#include "World/Items/WorldItemSystem.h"
#include "World/Math/GridMath.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace cutum
{
/// Explicit opt-in whole-window-frame benchmark. Only dedicated QA worlds may
/// be modified. Rendering, simulation, streaming and GUI remain the real paths.
class UItemPerformanceQa
{
public:
  UItemPerformanceQa(UWorld &world, const std::string &path,
                     const std::string &scene)
      : Path(path), Scene(scene)
  {
    const auto name = world.GetWorldName();
    if (name.size() < 2 || name.substr(name.size() - 2) != "QA")
      throw std::runtime_error("Item performance fixture requires a QA world");
    if (scene != "fp" && scene != "armor" && scene != "drops" &&
        scene != "catalog")
      throw std::runtime_error("Unknown item performance scene");
    auto *actor = world.GetControlledCreature();
    auto camera = world.GetCurrentUserCamera();
    if (!actor || !camera || actor->GetTypeId() != "human")
      throw std::runtime_error(
          "Item performance fixture requires a human player");
    Ids = world.GetItemDefinitionStorage()->ListCatalogIds();
    std::sort(Ids.begin(), Ids.end());
    for (const auto &id : Ids)
    {
      const auto model = IUPlatformPaths::Current().AssetRoot() /
                         world.GetItemDefinitionStorage()->Get(id)->ModelPath;
      std::ifstream file(model);
      nlohmann::json data;
      file >> data;
      if (data.value("extras", nlohmann::json::object())
              .value("design_version", 0) == 3)
        ++V3Count;
    }
    Allowed.insert(actor->GetId());
    if (Scene == "armor")
    {
      world.ForEachCreature(
          [&](const UCreature &other)
          {
            if (!other.IsPlayerCharacter() && other.GetTypeId() == "human" &&
                other.GetSpawnSource() == CreatureSpawnSource::Qa)
              ArmorIds.push_back(other.GetId());
          });
      std::sort(ArmorIds.begin(), ArmorIds.end());
      if (ArmorIds.size() != 12)
        throw std::runtime_error(
            "Armor benchmark requires 12 saved QA human NPCs");
      Allowed.insert(ArmorIds.begin(), ArmorIds.end());
    }
    RemoveOthers(world);
    UWorldItemSystem::Instance().Clear();
    world.SetGameMode(WorldGameMode::Creative);
    world.SetTimeOfDayNormalized(.35f);
    world.SetTimeFrozen(true);
    world.SetWeatherAutoEnabled(false);
    world.SetWeatherByName("clear", 0.f);
    world.SetViewSettings(WorldViewSettings{});
    camera->ApplyWorldViewSettings(world.GetViewSettings());
    for (int i = 0;
         i < 3 && camera->GetPerspective() != CameraPerspective::FirstPerson;
         ++i)
      camera->CycleFpsPerspective();
    camera->ResetAllKeyStatus();
    Origin = WorldPosToBlock(actor->GetBodyOrigin()) - glm::ivec3(0, 1, 0);
    for (int x = -12; x <= 12; ++x)
      for (int z = -6; z <= 30; ++z)
      {
        for (int y = 0; y <= 6; ++y)
        {
          const bool solid = world.GetBlockRegistry().BlocksMovement(
              world.GetBlockWorld().GetBlock(Origin + glm::ivec3(x, y, z)));
          if (solid != (y == 0))
            throw std::runtime_error(
                "Performance scene requires existing flat terrain");
        }
        const auto chunk =
            UChunkManager::WorldToChunk(Origin + glm::ivec3(x, 0, z));
        const glm::ivec2 column(chunk.x, chunk.z);
        if (std::find(Columns.begin(), Columns.end(), column) == Columns.end())
          Columns.push_back(column);
      }
    actor->SetBodyOrigin(glm::vec3(Origin) + glm::vec3(0, .501f, 0));
    camera->SetPosition(actor->GetBodyOrigin() + actor->GetEyeOffset());
    camera->SetOrientation(90, -8);
    actor->SetOrientation(90, -8);
    auto &inventory = actor->GetInventory();
    inventory = UCreatureInventory{};
    inventory.SetItemDefinitions(world.GetItemDefinitionStorage());
    inventory.SetCreative(true);
    if (Scene == "fp")
    {
      inventory.Insert(Entry("iron_pickaxe"));
      inventory.SetActiveSlot(0, 0);
    }
    if (Scene == "drops")
      for (int i = 0; i < 512; ++i)
      {
        const auto position =
            glm::vec3(Origin) +
            glm::vec3((i % 16 - 7.5f) * .7f, .7f, 5.f + (i / 16) * .7f);
        if (!UWorldItemSystem::Instance().SpawnStack(
                position, Entry(Ids[size_t(i) % Ids.size()]),
                InventoryDomain::Creative))
          throw std::runtime_error("Cannot create all 512 benchmark bodies");
      }
    if (Scene == "armor")
      SpawnArmor(world);
    Started = Clock::now();
    Frames.reserve(10000);
  }
  bool IsCatalog() const { return Scene == "catalog"; }
  bool IsFp() const { return Scene == "fp"; }
  bool IsDone() const { return Done; }
  bool NeedsCatalog() const
  {
    return IsCatalog() && TerrainReady && !CatalogStarted &&
           Seconds(Started) >= 8.;
  }
  void BeginCatalog()
  {
    CatalogStarted = true;
    ColdStart = Clock::now();
  }
  bool CatalogActive() const { return CatalogStarted && !CatalogComplete; }
  void SetCatalogReady(bool ready) { CatalogReady = ready; }
  const std::vector<std::string> &GetIds() const { return Ids; }
  void Update(UWorld &world)
  {
    if (!Done)
    {
      RemoveOthers(world);
      TerrainReady =
          std::all_of(Columns.begin(), Columns.end(), [&](const auto &column)
                      { return world.IsColumnVisualReady(column); });
    }
  }
  void Record(UWorld &world, const CreatureRenderStats &stats, int width,
              int height, double wallMs, double swapMs)
  {
    if (Done || !TerrainReady || Seconds(Started) < 8. ||
        (IsCatalog() && !CatalogStarted))
      return;
    if (CatalogStarted && !CatalogComplete)
    {
      ColdFrames.push_back(wallMs);
      if (!CatalogReady)
        return;
      ColdMs = Seconds(ColdStart) * 1000.;
      CatalogComplete = true;
      MeasureStart = Clock::now();
      return;
    }
    if (!Measuring)
    {
      Measuring = true;
      MeasureStart = Clock::now();
      AssetLookups = GetItemAssetLookupStats();
      TextureLookups = ItemGltfTextureCache::Instance().GetLookupStats();
    }
    Frames.push_back({{"wall_ms", wallMs},
                      {"swap_ms", swapMs},
                      {"world_tick_ms", world.GetLastWorldTickMs()},
                      {"app_ms", world.GetLastAppUpdateMs()},
                      {"draw_calls", stats.CreatureDrawCalls},
                      {"creatures_drawn", stats.CreaturesDrawn},
                      {"bodies", UWorldItemSystem::Instance().Count()}});
    MinDrawn = std::min(MinDrawn, stats.CreaturesDrawn);
    BodiesValid = BodiesValid && UWorldItemSystem::Instance().Count() ==
                                     (Scene == "drops" ? 512u : 0u);
    if (Seconds(MeasureStart) < 8. || Frames.size() < 120)
      return;
    Done = true;
    const auto memory = ItemGltfTextureCache::Instance().InspectMemory(true);
    const auto sharedMemory = ItemGltfTextureCache::Instance().InspectMemory();
    const bool passed = Frames.size() >= 120 && BodiesValid &&
                        (Scene != "armor" || MinDrawn >= 12) &&
                        memory.Textures > 0 && memory.InvalidTextures == 0 &&
                        width == 1920 && height == 1080;
    const auto assets = GetItemAssetLookupStats();
    const auto textures = ItemGltfTextureCache::Instance().GetLookupStats();
    nlohmann::json report = {
        {"passed", passed},
        {"scene", Scene},
        {"schema_version", 1},
        {"scope", "whole window frame: simulation, streaming, rendering, GUI "
                  "and swap; vsync forced off"},
        {"resolution", {width, height}},
        {"warmup_sec", 8},
        {"measure_sec", Seconds(MeasureStart)},
        {"catalog_item_count", Ids.size()},
        {"runtime_v3_count", V3Count},
        {"frames", Frames},
        {"cold_catalog_ms", ColdMs},
        {"cold_frames_ms", ColdFrames},
        {"item_texture_count", memory.Textures},
        {"shared_cache_rgba_mip_bytes", sharedMemory.RgbaMipBytes},
        {"invalid_texture_count", memory.InvalidTextures},
        {"texture_dimensions_width_height_levels", memory.Dimensions},
        {"measured_asset_canonical_resolutions",
         assets.CanonicalResolutions - AssetLookups.CanonicalResolutions},
        {"measured_asset_fast_hits", assets.FastHits - AssetLookups.FastHits},
        {"measured_texture_canonical_resolutions",
         textures.CanonicalResolutions - TextureLookups.CanonicalResolutions},
        {"measured_texture_fast_hits",
         textures.FastHits - TextureLookups.FastHits},
        {"terrain_columns", Columns.size()},
        {"item_texture_rgba_mip_bytes", memory.RgbaMipBytes},
        {"extra_creatures_removed", Removed},
        {"expected_armor_creatures", Scene == "armor" ? 12 : 0},
        {"gpu", reinterpret_cast<const char *>(glGetString(GL_RENDERER))}};
    std::filesystem::create_directories(Path.parent_path());
    std::ofstream(Path) << report.dump(2) << '\n';
  }

private:
  using Clock = std::chrono::steady_clock;
  static double Seconds(Clock::time_point from)
  {
    return std::chrono::duration<double>(Clock::now() - from).count();
  }
  static InventoryEntryRef Entry(const std::string &id)
  {
    InventoryEntryRef entry;
    entry.empty = false;
    entry.kind = InventoryEntryKind::Item;
    entry.Id = id;
    entry.count = 1;
    return entry;
  }
  void RemoveOthers(UWorld &world)
  {
    std::vector<uint64_t> remove;
    world.ForEachCreature(
        [&](const UCreature &creature)
        {
          if (Allowed.find(creature.GetId()) == Allowed.end())
            remove.push_back(creature.GetId());
        });
    for (auto id : remove)
      world.RemoveCreature(id);
    Removed += remove.size();
  }
  void SpawnArmor(UWorld &world)
  {
    const char *tiers[] = {"leather", "copper", "iron", "gold"};
    const char *slots[] = {"head", "chest", "legs", "feet"};
    for (int i = 0; i < 12; ++i)
    {
      const auto position =
          glm::vec3(Origin) +
          glm::vec3((i % 4 - 1.5f) * 1.8f, .501f, 7.f + (i / 4) * 2.f);
      const auto id = ArmorIds[size_t(i)];
      auto *actor = world.GetCreature(id);
      if (!actor)
        throw std::runtime_error("Cannot spawn benchmark armor actor");
      actor->SetBodyOrigin(position);
      Allowed.insert(id);
      HumanAppearance appearance;
      appearance.bodyVariant = i % 2 ? "female" : "male";
      actor->SetHumanAppearance(appearance);
      actor->SetOrientation(-90, 0);
      actor->GetInventory().SetItemDefinitions(
          world.GetItemDefinitionStorage());
      for (size_t slot = 0; slot < 4; ++slot)
        if (!actor->GetInventory().EquipArmor(
                slot, Entry(std::string(tiers[i % 4]) + "_" + slots[slot]),
                *world.GetItemDefinitionStorage(), false))
          throw std::runtime_error("Cannot equip benchmark armor actor");
    }
  }
  std::filesystem::path Path;
  std::string Scene;
  Clock::time_point Started, ColdStart, MeasureStart;
  std::vector<std::string> Ids;
  std::vector<uint64_t> ArmorIds;
  std::unordered_set<uint64_t> Allowed;
  std::vector<nlohmann::json> Frames;
  std::vector<double> ColdFrames;
  std::vector<glm::ivec2> Columns;
  ItemAssetLookupStats AssetLookups;
  ItemGltfTextureCache::LookupStats TextureLookups;
  glm::ivec3 Origin{};
  uint32_t MinDrawn{UINT32_MAX};
  size_t V3Count{0}, Removed{0};
  bool Done{false}, Measuring{false}, BodiesValid{true}, TerrainReady{false};
  bool CatalogStarted{false}, CatalogReady{false}, CatalogComplete{false};
  double ColdMs{0};
};
} // namespace cutum
