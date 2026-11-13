#include "Game/Survival/SurvivalSystems.h"
#include "Blocks/BlockRegistry.h"
#include "Creatures/Core/Creature.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ToolCapabilities.h"
#include "World/Core/World.h"
#include "World/Items/WorldItemSystem.h"
#include "World/Math/GridMath.h"
#include "World/Physics/CollisionReadiness.h"
#include "World/Raycast/BlockRaycast.h"
#include <algorithm>
#include <cmath>
namespace cutum
{
namespace
{
InventoryEntryRef Entry(std::string id, int count = 1)
{
  InventoryEntryRef e;
  e.kind = InventoryEntryKind::Item;
  e.Id = std::move(id);
  e.count = count;
  e.empty = false;
  return e;
}
std::string Name(UWorld &w, glm::ivec3 p)
{
  return w.GetBlockRegistry().GetTypeNameById(w.GetBlockWorld().GetBlock(p));
}
bool Water(UWorld &w, glm::ivec3 p)
{
  return w.GetBlockRegistry()
             .Physics(w.GetBlockWorld().GetBlock(p))
             .FluidKindPreset == FluidKind::Water;
}
} // namespace
USurvivalSystems &USurvivalSystems::Instance()
{
  static USurvivalSystems s;
  return s;
}
bool USurvivalSystems::NearStation(UWorld &w, glm::vec3 position,
                                   const std::string &station) const
{
  if (station.empty() || w.GetGameMode() == WorldGameMode::Creative)
    return true;
  auto p = WorldPosToBlock(position);
  for (int x = -3; x <= 3; ++x)
    for (int z = -3; z <= 3; ++z)
      for (int y = -2; y <= 2; ++y)
      {
        const auto name = Name(w, p + glm::ivec3(x, y, z));
        if (name == station || (station == "furnace" && name == "furnace_lit"))
          return true;
      }
  return false;
}
bool USurvivalSystems::Use(UWorld &w, UCreature &actor, glm::vec3 eye,
                           glm::vec3 dir, std::string &reason)
{
  auto &inv = actor.GetInventory();
  auto *e = inv.GetActiveEntryRef();
  if (!e || e->kind != InventoryEntryKind::Item)
    return false;
  const auto *defs = w.GetItemDefinitionStorage();
  const auto *def = defs ? defs->Get(e->Id) : nullptr;
  if (!def)
    return false;
  const auto action = def->Use.Interaction;
  if (action.empty())
    return false;
  if (e->broken)
  {
    reason = "Item is broken";
    return true;
  }
  auto hit = RaycastSolidBlocks(w.GetBlockWorld(), w.GetBlockRegistry(), eye,
                                dir, 4.f, BlockRaycastTargetMode::Pointable);
  if (action == "container")
  {
    // Stop at the first opaque node: a water source behind a wall is
    // unreachable.
    std::optional<glm::ivec3> water;
    float limit = hit ? hit->distance : 4.f;
    for (float t = 0; t <= limit; t += .05f)
    {
      auto p = WorldPosToBlock(eye + dir * t);
      if (Water(w, p))
      {
        water = p;
        break;
      }
    }
    if (water)
    {
      if (e->Id == "bucket" && e->contentsUnits > 0)
      {
        reason = "Bucket is already full";
        return true;
      }
      if (e->Id == "bucket" && !w.ReplaceBlockAt(*water, "air"))
      {
        reason = "Water is unavailable";
        return true;
      }
      e->contentsId = "water";
      e->contentsUnits = e->Id == "bucket" ? 1 : 4;
      reason = "Filled with water";
      return true;
    }
    if (e->Id == "waterskin")
      return false; // Normal drink action uses finite contents.
    if (e->contentsId != "water" || e->contentsUnits <= 0)
    {
      reason = "Empty bucket: aim at water";
      return true;
    }
    if (!hit)
    {
      reason = "Aim at a surface";
      return true;
    }
    auto pos = hit->blockPos + hit->faceNormal;
    if (!w.AddObject("water", glm::vec3(pos)))
    {
      reason = "No room for water";
      return true;
    }
    if (!inv.IsCreative())
    {
      e->contentsUnits = 0;
      e->contentsId.clear();
    }
    reason = "Water poured";
    return true;
  }
  if (action == "shear")
  {
    auto target = w.PickCreatureByView(eye, dir, 3.f);
    if (target)
    {
      auto *c = w.GetCreature(*target);
      if (!c || c->GetTypeId() != "sheep")
      {
        reason = "This creature has no harvestable fleece";
        return true;
      }
      auto obstruction = RaycastSolidBlocks(
          w.GetBlockWorld(), w.GetBlockRegistry(), eye,
          glm::normalize(c->GetCollisionVolume().center - eye),
          glm::distance(eye, c->GetCollisionVolume().center));
      if (obstruction)
      {
        reason = "Target is obstructed";
        return true;
      }
      if (std::any_of(Sheared.begin(), Sheared.end(),
                      [&](const auto &s) { return s.creature == *target; }))
      {
        reason = "Fleece is regrowing";
        return true;
      }
      UWorldItemSystem::Instance().SpawnStack(
          c->GetBodyOrigin() + glm::vec3(0, .5f, 0), Entry("wool", 3));
      Sheared.push_back({*target, 300});
    }
    else if (hit)
    {
      auto n = Name(w, hit->blockPos);
      const auto *bd = w.GetBlockRegistry().GetDefinitions();
      if (n.find("grass") == std::string::npos &&
          n.find("leaves") == std::string::npos &&
          n.find("fern") == std::string::npos)
      {
        reason = "No harvestable vegetation";
        return true;
      }
      if (!w.DelBlockAt(hit->blockPos))
      {
        reason = "Harvest failed";
        return true;
      }
      UWorldItemSystem::Instance().SpawnStack(glm::vec3(hit->blockPos),
                                              Entry("plant_fiber", 2));
    }
    else
    {
      reason = "Aim at fleece or vegetation";
      return true;
    }
  }
  else
  {
    if (!hit)
    {
      reason = "Aim at a surface";
      return true;
    }
    auto pos = hit->blockPos;
    auto name = Name(w, pos);
    if (action == "till")
    {
      if (name != "dirt" && name != "grass" && name != "mtg_dirt_with_grass")
      {
        reason = "Till dirt or grass";
        return true;
      }
      if (hit->faceNormal.y != 1 ||
          w.GetBlockWorld().GetBlock(pos + glm::ivec3(0, 1, 0)) != BLOCK_AIR)
      {
        reason = "Clear the top of the soil";
        return true;
      }
      if (!w.ReplaceBlockAt(pos, "farmland_dry"))
      {
        reason = "Soil could not be tilled";
        return true;
      }
    }
    else if (action == "plant")
    {
      if (name != "farmland_dry" && name != "farmland_wet")
      {
        reason = "Plant seeds on tilled soil";
        return true;
      }
      auto above = pos + glm::ivec3(0, 1, 0);
      if (w.GetBlockWorld().GetBlock(above) != BLOCK_AIR)
      {
        reason = "Crop space is occupied";
        return true;
      }
      auto trial = inv;
      if (!trial.Consume(InventoryEntryKind::Item, e->Id))
      {
        reason = "No seeds";
        return true;
      }
      if (!w.AddObject("cu_crop_sprout", glm::vec3(above)))
      {
        reason = "Planting failed";
        return true;
      }
      inv = std::move(trial);
      Crops.push_back({pos, 0, 0});
      reason = "Seeds planted";
      return true;
    }
    else if (action == "ignite")
    {
      auto air = pos + hit->faceNormal;
      if (w.GetBlockWorld().GetBlock(air) != BLOCK_AIR)
      {
        reason = "No room for fire";
        return true;
      }
      if (!w.GetBlockRegistry().IsFlammable(w.GetBlockWorld().GetBlock(pos)))
      {
        reason = "Needs a flammable surface";
        return true;
      }
      if (!w.AddObject("fire", glm::vec3(air)))
      {
        reason = "Ignition failed";
        return true;
      }
    }
    else
      return false;
  }
  if (auto *active = inv.GetActiveEntryRef())
    ApplyItemWear(*active, *def,
                  1.f / std::max(1, def->Tool.PunchAttackUses > 0
                                        ? def->Tool.PunchAttackUses
                                        : 200),
                  IsToolWearEnabled(w.GetGameMode(), w.GetDifficulty()));
  reason = "Used " + def->DisplayName;
  return true;
}
void USurvivalSystems::BlockDrops(UWorld &w, glm::ivec3 p,
                                  const std::string &id)
{
  std::string drop = id;
  int count = 1;
  bool block = true;
  if (id == "tree_bark" || id == "tree_branch" || id == "mtg_tree" ||
      id == "mtg_jungletree" || id == "mtg_acacia_tree" ||
      id == "mtg_aspen_tree")
    drop = "wood";
  else if (id == "mtg_stone")
    drop = "stone";
  else if (id == "oreIron" || id == "mtg_stone_with_iron")
  {
    drop = "raw_iron";
    block = false;
  }
  else if (id == "oreGold" || id == "mtg_stone_with_gold")
  {
    drop = "raw_gold";
    block = false;
  }
  else if (id == "mtg_stone_with_copper" || id == "oreCopper")
  {
    drop = "raw_copper";
    block = false;
  }
  else if (id == "oreCoal" || id == "mtg_stone_with_coal")
  {
    drop = "coal";
    block = false;
  }
  else if (id == "wheat" || id == "cu_crop_ripe")
  {
    UWorldItemSystem::Instance().Spawn(glm::vec3(p), "seeds", 2, false);
    drop = "grain";
    count = 3;
    block = false;
  }
  else if (id == "cu_crop_sprout" || id == "cu_crop_young")
  {
    drop = "seeds";
    block = false;
  }
  else if (id.find("grass") != std::string::npos && id != "grass" &&
           id.find("dirt") == std::string::npos)
  {
    drop = "plant_fiber";
    block = false;
    UWorldItemSystem::Instance().Spawn(glm::vec3(p), "seeds", 1, false);
  }
  else if (id.find("leaves") != std::string::npos)
  {
    drop = "plant_fiber";
    block = false;
  }
  UWorldItemSystem::Instance().Spawn(glm::vec3(p), drop, count, block);
}
void USurvivalSystems::Tick(UWorld &w, float dt)
{
  for (auto &s : Sheared)
    s.remaining -= std::max(0.f, dt);
  Sheared.erase(std::remove_if(Sheared.begin(), Sheared.end(),
                               [](const auto &s) { return s.remaining <= 0; }),
                Sheared.end());
  TickDebt += std::max(0.f, dt);
  if (TickDebt < 1.f)
    return;
  const float step = std::min(TickDebt, 5.f);
  TickDebt = 0;
  for (auto &c : Crops)
  {
    if (!IsCollisionRingReady(w.GetBlockWorld(), c.ground, 0))
      continue;
    auto above = c.ground + glm::ivec3(0, 1, 0);
    auto ground = Name(w, c.ground), plant = Name(w, above);
    if ((ground != "farmland_dry" && ground != "farmland_wet") ||
        plant.rfind("cu_crop_", 0) != 0)
    {
      c.growth = -1;
      continue;
    }
    bool wet = false;
    for (int x = -4; x <= 4 && !wet; ++x)
      for (int z = -4; z <= 4 && !wet; ++z)
        for (int y = 0; y <= 1; ++y)
          wet |= Water(w, c.ground + glm::ivec3(x, y, z));
    c.moisture = wet ? 60.f : std::max(0.f, c.moisture - step);
    if ((c.moisture > 0) != (ground == "farmland_wet"))
      w.ReplaceBlockAt(c.ground,
                       c.moisture > 0 ? "farmland_wet" : "farmland_dry");
    c.growth = std::min(180.f, c.growth + step * (c.moisture > 0 ? 1.f : .15f));
    std::string desired = c.growth >= 180  ? "cu_crop_ripe"
                          : c.growth >= 60 ? "cu_crop_young"
                                           : "cu_crop_sprout";
    if (plant != desired)
      w.ReplaceBlockAt(above, desired);
  }
  Crops.erase(std::remove_if(Crops.begin(), Crops.end(),
                             [](const auto &c) { return c.growth < 0; }),
              Crops.end());
}
void USurvivalSystems::Clear()
{
  Crops.clear();
  Sheared.clear();
  TickDebt = 0;
}
void USurvivalSystems::Serialize(nlohmann::json &j) const
{
  auto crops = nlohmann::json::array(), sheared = nlohmann::json::array();
  for (const auto &c : Crops)
    crops.push_back({{"position", {c.ground.x, c.ground.y, c.ground.z}},
                     {"growth", c.growth},
                     {"moisture", c.moisture}});
  for (const auto &s : Sheared)
    sheared.push_back({{"creature", s.creature}, {"remaining", s.remaining}});
  j["survival_v1"] = {{"crops", crops}, {"sheared", sheared}};
}
void USurvivalSystems::Deserialize(const nlohmann::json &j)
{
  Clear();
  if (!j.contains("survival_v1"))
    return;
  const auto &r = j["survival_v1"];
  if (r.contains("crops") && r["crops"].is_array())
    for (const auto &c : r["crops"])
      try
      {
        auto p = c.at("position");
        Crops.push_back(
            {{p.at(0).get<int>(), p.at(1).get<int>(), p.at(2).get<int>()},
             std::clamp(c.value("growth", 0.f), 0.f, 180.f),
             std::clamp(c.value("moisture", 0.f), 0.f, 60.f)});
      }
      catch (const nlohmann::json::exception &)
      {
      }
  if (r.contains("sheared") && r["sheared"].is_array())
    for (const auto &s : r["sheared"])
      try
      {
        Sheared.push_back({s.at("creature").get<uint64_t>(),
                           std::clamp(s.value("remaining", 0.f), 0.f, 300.f)});
      }
      catch (const nlohmann::json::exception &)
      {
      }
}
} // namespace cutum
