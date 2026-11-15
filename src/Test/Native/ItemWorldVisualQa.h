#pragma once
#include "App/Application.h"
#include "Creatures/Core/Creature.h"
#include "Items/ItemDefinitionStorage.h"
#include "Render/Camera/Camera.h"
#include "Render/GlIncludes.h"
#include "ThirdParty/stb_image_write.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/World.h"
#include "World/Items/WorldItemSystem.h"
#include "World/Math/GridMath.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace cutum
{
/// Opt-in actual-world presentation capture. The runner restores the dedicated
/// QA world; no synthetic preview or alternate item renderer is involved.
class UItemWorldVisualQa
{
public:
  UItemWorldVisualQa(UWorld &world, const std::filesystem::path &path)
      : Path(path)
  {
    if (world.GetWorldName().size() < 2 ||
        world.GetWorldName().substr(world.GetWorldName().size() - 2) != "QA")
      throw std::runtime_error(
          "World item capture requires a dedicated QA world");
    std::ifstream input(path.parent_path() / "request.json");
    input >> Request;
    Ids = Request.at("ids").get<std::vector<std::string>>();
    if (Ids.empty())
      throw std::runtime_error("World item capture has no IDs");
    auto *actor = world.GetControlledCreature();
    auto camera = world.GetCurrentUserCamera();
    if (!actor || !camera || actor->GetTypeId() != "human")
      throw std::runtime_error("World item capture requires a human");
    std::vector<uint64_t> others;
    world.ForEachCreature(
        [&](const UCreature &c)
        {
          if (c.GetId() != actor->GetId())
            others.push_back(c.GetId());
        });
    for (auto id : others)
      world.RemoveCreature(id);
    Base = WorldPosToBlock(actor->GetBodyOrigin()) - glm::ivec3(0, 1, 0);
    const auto stone = world.GetBlockRegistry().GetIdByTypeName("stone");
    std::vector<glm::ivec3> edits;
    for (int x = -5; x <= 5; ++x)
      for (int z = -4; z <= 10; ++z)
        for (int y = 0; y <= 5; ++y)
        {
          const auto pos = Base + glm::ivec3(x, y, z);
          const bool room =
              Request.at("light").get<std::string>().find("interior") == 0;
          const auto block = y == 0 || (room && (y == 5 || x == -5 || x == 5 ||
                                                 z == -4 || z == 10))
                                 ? stone
                                 : BLOCK_AIR;
          if (world.GetBlockWorld().GetBlock(pos) != block)
          {
            world.GetBlockWorld().SetBlock(pos, block);
            edits.push_back(pos);
          }
        }
    world.RelightPlayerEdit(edits, Base.y);
    world.InvalidateBlockMesh();
    if (Request.at("light") == "interior-lit" &&
        !world.AddObject("torch",
                         glm::vec3(Base) + glm::vec3(-.5f, 1.5f, 1.5f)))
      throw std::runtime_error("Cannot place the real room light source");
    world.SetGameMode(WorldGameMode::Creative);
    world.SetTimeOfDayNormalized(Request.at("light") == "night" ? .85f : .35f);
    world.SetTimeFrozen(true);
    world.SetWeatherAutoEnabled(false);
    world.SetWeatherByName("clear", 0.f);
    while (camera->GetPerspective() != CameraPerspective::FirstPerson)
      camera->CycleFpsPerspective();
    camera->ResetAllKeyStatus();
    Report = {{"passed", false},
              {"scope", "actual world FP and physical drop rendering"},
              {"request", Request},
              {"frames", nlohmann::json::array()}};
    Started = Clock::now();
    SetItem(world);
  }
  void Update(UWorld &world)
  {
    if (Done)
      return;
    auto *actor = world.GetControlledCreature();
    auto camera = world.GetCurrentUserCamera();
    std::vector<uint64_t> others;
    world.ForEachCreature(
        [&](const UCreature &c)
        {
          if (c.GetId() != actor->GetId())
            others.push_back(c.GetId());
        });
    for (auto id : others)
      world.RemoveCreature(id);
    const auto feet = glm::vec3(Base) + glm::vec3(0, .501f, 0);
    actor->SetBodyOrigin(feet);
    actor->SetOrientation(90, -12);
    camera->SetPosition(feet + actor->GetEyeOffset());
    camera->SetOrientation(90, -12);
    Ready = true;
    for (int x = -1; x <= 1; ++x)
      for (int z = -1; z <= 1; ++z)
        Ready = Ready && world.IsColumnVisualReady(glm::ivec2(
                             UChunkManager::WorldToChunk(Base).x + x,
                             UChunkManager::WorldToChunk(Base).z + z));
  }
  bool Capture(UWorld &world, int width, int height)
  {
    if (Done || !Ready || Seconds(Started) < 5. || Seconds(ItemStarted) < .45)
      return Done;
    if (width != Request.at("width") || height != Request.at("height"))
      throw std::runtime_error("Unexpected world capture framebuffer size");
    const auto name = Ids[Index] + ".png";
    std::vector<unsigned char> pixels(size_t(width) * height * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    for (int y = 0; y < height / 2; ++y)
      std::swap_ranges(pixels.begin() + size_t(y) * width * 4,
                       pixels.begin() + size_t(y + 1) * width * 4,
                       pixels.begin() + size_t(height - 1 - y) * width * 4);
    if (!stbi_write_png((Path.parent_path() / name).string().c_str(), width,
                        height, 4, pixels.data(), width * 4))
      throw std::runtime_error("Cannot write world item capture");
    const auto *actor = world.GetControlledCreature();
    const auto &env = world.GetEnvironmentState();
    const auto cell = WorldPosToBlock(actor->GetEyePosition());
    const auto coord = UChunkManager::WorldToChunk(cell);
    const auto *chunk = world.GetBlockWorld().GetChunkManager().GetChunk(coord);
    const int light =
        chunk ? int(chunk->GetLightPackedLocal(cell - coord * CHUNK_SIZE)) : -1;

    const auto bodies = UWorldItemSystem::Instance().GetItems();
    const bool bodyPresent = std::any_of(bodies.begin(), bodies.end(),
                                         [&](const auto &body)
                                         {
                                           return body.id == BodyId &&
                                                  body.stack.Id == Ids[Index] &&
                                                  body.stack.count == 1;
                                         });
    Report["frames"].push_back(
        {{"id", Ids[Index]},
         {"file", name},
         {"time", env.TimeOfDayNormalized},
         {"day_factor", env.DayNightFactor},
         {"eye_light_packed", light},
         {"active_item", actor->GetInventory().GetActiveEntryRef()->Id},
         {"physical_bodies", UWorldItemSystem::Instance().Count()},
         {"matching_body", bodyPresent}});
    ++Index;
    if (Index == Ids.size())
    {
      Done = true;
      Report["passed"] = true;
      std::ofstream(Path) << Report.dump(2) << '\n';
    }
    else
      SetItem(world);
    return Done;
  }

private:
  using Clock = std::chrono::steady_clock;
  static double Seconds(Clock::time_point time)
  {
    return std::chrono::duration<double>(Clock::now() - time).count();
  }
  void SetItem(UWorld &world)
  {
    auto &inv = world.GetControlledCreature()->GetInventory();
    inv = UCreatureInventory{};
    inv.SetItemDefinitions(world.GetItemDefinitionStorage());
    inv.SetCreative(true);
    InventoryEntryRef entry;
    entry.empty = false;
    entry.kind = InventoryEntryKind::Item;
    entry.Id = Ids[Index];
    entry.count = 1;
    inv.Insert(entry);
    inv.SetActiveSlot(0, 0);
    auto &physics = UWorldItemSystem::Instance();
    physics.Clear();
    BodyId = physics.SpawnStack(glm::vec3(Base) + glm::vec3(-.55f, 1.1f, 2.1f),
                                entry, InventoryDomain::Creative);
    if (!BodyId)
      throw std::runtime_error("Cannot spawn world presentation item");
    ItemStarted = Clock::now();
  }
  std::filesystem::path Path;
  nlohmann::json Request, Report;
  std::vector<std::string> Ids;
  size_t Index{0};
  int BodyId{0};
  glm::ivec3 Base{};
  Clock::time_point Started, ItemStarted;
  bool Ready{false}, Done{false};
};
} // namespace cutum
