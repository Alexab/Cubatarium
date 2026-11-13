#pragma once
#include "Blocks/Input/PlayerInteractionRouter.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Influence/InfluenceApplier.h"
#include "Creatures/Influence/InfluenceResolver.h"
#include "Game/Crafting/RecipeRegistry.h"
#include "Game/Survival/SurvivalSystems.h"
#include "Items/ItemDefinitionStorage.h"
#include "Render/Camera/Camera.h"
#include "World/Core/World.h"
#include "World/Interaction/BlockPlacementService.h"
#include "World/Items/WorldItemSystem.h"
#include "World/Math/GridMath.h"
namespace cutum
{
// Accelerated survival integration: fixture terrain supplies natural nodes,
// never inventory grants.
inline nlohmann::json RunSurvivalLoopQa(UWorld &world, glm::ivec3 floor)
{
  nlohmann::json result = {{"passed", false},
                           {"steps", nlohmann::json::array()}};
  auto *actor = world.GetControlledCreature();
  auto camera = world.GetCurrentUserCamera();
  if (!actor || !camera)
    return result;
  auto &inv = actor->GetInventory();
  auto &physics = WorldItemSystem::Instance();
  auto &survival = USurvivalSystems::Instance();
  const auto original = actor->GetBodyOrigin();
  const auto eye = camera->GetPosition();
  world.SetGameMode(WorldGameMode::Survival);
  physics.Clear();
  inv = UCreatureInventory{};
  inv.SetItemDefinitions(world.GetItemDefinitionStorage());
  auto note = [&](std::string step, bool ok)
  {
    result["steps"].push_back({{"name", step}, {"passed", ok}});
    return ok;
  };
  auto stand = [&](glm::vec3 position)
  {
    actor->SetBodyOrigin(position);
    camera->SetPosition(position + actor->GetEyeOffset());
    world.SyncPhysicalCreatureIndex();
  };
  auto select = [&](const std::string &id)
  {
    for (size_t i = 0; i < 40; ++i)
      if (!inv.GetSlot(i).empty && inv.GetSlot(i).Id == id)
      {
        inv.SetActiveSlot(i / 10, i % 10);
        return true;
      }
    return false;
  };
  URecipeRegistry recipes;
  recipes.LoadFromDirectory("content/recipes");
  auto craft = [&](const char *id, int count = 1)
  {
    const auto *r = recipes.FindById(id);
    if (!r)
      return false;
    for (int n = 0; n < count; ++n)
      if (!recipes.TryCraft(
              inv, *r,
              survival.NearStation(world, actor->GetBodyOrigin(), r->Station)))
      {
        result["failed_recipe"] = id;
        return false;
      }
    return true;
  };
  auto natural = [&](std::initializer_list<const char *> names)
  {
    for (const auto *id : names)
      if (world.GetBlockRegistry().GetIdByTypeName(id) != BLOCK_AIR)
        return std::string(id);
    return std::string();
  };
  const auto node = floor + glm::ivec3(0, 1, 4);
  auto harvestAt = [&](glm::ivec3 pos)
  {
    stand(glm::vec3(pos) + glm::vec3(0, -.499f, -1.f));
    PlayerInteractionRouter::BeginDigIntent(*actor, pos);
    for (int frame = 0;
         frame < 1000 && world.GetBlockWorld().GetBlock(pos) != BLOCK_AIR;
         ++frame)
    {
      auto prediction = InfluenceResolver::Resolve(
          world, *actor, world.GetGameMode(), nullptr);
      if (!prediction.Valid || prediction.Cancelled)
      {
        result["harvest_cancel"] = prediction.CancelReason;
        result["harvest_block_id"] = world.GetBlockWorld().GetBlock(pos);
        result["harvest_pos"] = {pos.x, pos.y, pos.z};
        actor->ClearIntent();
        return false;
      }
      InfluenceApplier::Apply(world, prediction, world.GetGameMode(), .1f);
    }
    actor->ClearIntent();
    if (world.GetBlockWorld().GetBlock(pos) != BLOCK_AIR)
      return false;
    for (int frame = 0; frame < 65; ++frame)
      physics.Tick(world, 1.f / 60);
    bool picked = physics.Count() == 0;
    if (!picked)
    {
      result["unpicked_count"] = physics.Count();
      result["grain_count"] = inv.Count(InventoryEntryKind::Item, "grain");
      nlohmann::json bodies;
      physics.Serialize(bodies);
      result["unpicked"] = bodies;
    }
    return picked;
  };
  auto harvest = [&](const std::string &type, int count)
  {
    if (type.empty())
      return false;
    for (int n = 0; n < count; ++n)
    {
      if (!world.ReplaceBlockAt(node, type) || !harvestAt(node))
        return false;
    }
    return true;
  };
  auto place = [&](const char *id, glm::ivec3 ground)
  {
    if (!select(id))
      return false;
    stand(glm::vec3(floor) + glm::vec3(0, .501f, 3));
    const auto aim = glm::normalize(glm::vec3(ground) + glm::vec3(0, .5f, 0) -
                                    camera->GetPosition());
    return UBlockPlacementService::AddObjectByView(world, camera->GetPosition(),
                                                   aim);
  };
  bool ok = true;
  auto step = [&](const char *name, bool value)
  {
    ok = ok && value;
    return note(name, value);
  };
  if (!step("empty inventory gathers natural wood by hand",
            inv.GetStorage().empty() &&
                harvest(natural({"tree_bark", "wood"}), 20)))
    goto done;
  if (!step("hand recipes produce sticks, wooden pick and workbench",
            craft("stick") && craft("wood_pickaxe") && craft("crafting_table")))
    goto done;
  if (!step("workbench placed through resource-consuming placement",
            place("crafting_table", floor + glm::ivec3(2, 0, 3))))
    goto done;
  if (!step("wooden pick mines stone; furnace and stone pick crafted",
            select("wood_pickaxe") && harvest("stone", 15) &&
                craft("furnace") && craft("stone_pickaxe")))
    goto done;
  if (!step("furnace placed from its actual slot",
            place("furnace", floor + glm::ivec3(-2, 0, 3))))
    goto done;
  if (!step("stone pick obtains ore and fuel",
            select("stone_pickaxe") &&
                harvest(natural({"mtg_stone_with_iron", "oreIron"}), 3) &&
                harvest(natural({"mtg_stone_with_coal", "oreCoal"}), 3)))
    goto done;
  if (!step("ore and coal smelt into iron; upgrade pick",
            craft("iron_ingot", 3) && craft("stick") && craft("iron_pickaxe")))
    goto done;
  if (!step("iron pick supplies the complete iron baseline",
            select("iron_pickaxe") &&
                harvest(natural({"mtg_stone_with_iron", "oreIron"}), 54) &&
                harvest(natural({"mtg_stone_with_coal", "oreCoal"}), 59) &&
                craft("iron_ingot", 54)))
    goto done;
  inv.SetActiveSlot(3, 9);
  if (!step("vegetation supplies seed and fiber; cord is craftable",
            harvest(natural({"tall_grass", "mtg_grass_1"}), 12) &&
                craft("cord", 4)))
    goto done;
  if (!step("five iron tools and four armor slots craftable",
            craft("stick", 3) && craft("iron_axe") && craft("iron_shovel") &&
                craft("iron_hoe") && craft("iron_sword") &&
                craft("iron_head") && craft("iron_chest") &&
                craft("iron_legs") && craft("iron_feet")))
    goto done;
  if (!step("utilities, shield, anvil and bow use real resources",
            craft("bucket") && craft("shears") && craft("iron_shield") &&
                craft("flint_and_steel") && craft("anvil") &&
                craft("wood_bow")))
    goto done;
  if (!step("anvil placement", place("anvil", floor + glm::ivec3(2, 0, 5))))
    goto done;
  {
    auto soil = floor + glm::ivec3(-1, 0, 5);
    world.ReplaceBlockAt(soil, "dirt");
    select("iron_hoe");
    std::string reason;
    stand(glm::vec3(floor) + glm::vec3(0, .501f, 3));
    auto aim = glm::normalize(glm::vec3(soil) + glm::vec3(0, .5f, 0) -
                              camera->GetPosition());
    survival.Use(world, *actor, camera->GetPosition(), aim, reason);
    result["till_reason"] = reason;
    select("seeds");
    survival.Use(world, *actor, camera->GetPosition(), aim, reason);
    result["plant_reason"] = reason;
    world.ReplaceBlockAt(soil + glm::ivec3(1, 0, 0), "water");
    for (int n = 0; n < 181; ++n)
      survival.Tick(world, 1.f);
    inv.SetActiveSlot(3, 9);
    result["crop_before_harvest"] = world.GetBlockRegistry().GetTypeNameById(
        world.GetBlockWorld().GetBlock(soil + glm::ivec3(0, 1, 0)));
    if (!step("tilled, watered crop matures and is harvested",
              harvestAt(soil + glm::ivec3(0, 1, 0)) &&
                  inv.Count(InventoryEntryKind::Item, "grain") >= 3))
      goto done;
    if (!step("harvest and fuel bake edible bread", craft("bread")))
      goto done;
    select("bread");
    actor->AdvanceInfluenceCooldown(2);
    actor->GetVitals().satiety = 40;
    PlayerInteractionRouter::SetUseIntent(*actor);
    auto use =
        InfluenceResolver::Resolve(world, *actor, world.GetGameMode(), nullptr);
    InfluenceApplier::Apply(world, use, world.GetGameMode(), 0);
    actor->ClearIntent();
    if (!step("cooked harvest restores survival nutrition",
              actor->GetVitals().satiety > 40))
      goto done;
    world.ReplaceBlockAt(soil + glm::ivec3(1, 0, 0), "air");
  }
  result["passed"] = ok;
done:
  actor->ClearIntent();
  world.CancelBreakSession();
  physics.Clear();
  stand(original);
  camera->SetPosition(eye);
  return result;
}
} // namespace cutum
