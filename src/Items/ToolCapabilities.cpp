#include "Items/ToolCapabilities.h"
#include "Blocks/BlockDigRules.h"
#include "Game/Inventory/InventoryTypes.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

bool IsToolWearEnabled(WorldGameMode mode, WorldDifficulty difficulty)
{
  if (mode == WorldGameMode::Creative)
  {
    return false;
  }
  // Peaceful = easiest Survival tier (difficulty agent).
  if (difficulty == WorldDifficulty::Peaceful)
  {
    return false;
  }
  return true;
}

std::unordered_map<std::string, int>
InferDigGroups(const BlockDefinition &block)
{
  if (!block.DigGroups.empty())
  {
    return block.DigGroups;
  }
  std::unordered_map<std::string, int> groups;
  groups["level"] = block.DigLevel;
  auto has_type = [&](const char *name)
  {
    for (const auto &t : block.Types)
    {
      if (t == name)
      {
        return true;
      }
    }
    return false;
  };
  const std::string &n = block.Name;
  if (has_type("plants") || n.find("leaves") != std::string::npos ||
      n.find("grass") != std::string::npos ||
      n.find("flower") != std::string::npos)
  {
    groups["snappy"] = 3;
    groups["oddly_breakable_by_hand"] = 3;
  }
  else if (n.find("wood") != std::string::npos ||
           n.find("tree") != std::string::npos ||
           n.find("log") != std::string::npos ||
           n.find("plank") != std::string::npos || has_type("trees"))
  {
    groups["choppy"] = 2;
  }
  else if (n.find("dirt") != std::string::npos ||
           n.find("sand") != std::string::npos ||
           n.find("gravel") != std::string::npos ||
           n.find("clay") != std::string::npos ||
           n.find("snow") != std::string::npos)
  {
    groups["crumbly"] = 2;
    groups["oddly_breakable_by_hand"] = 2;
  }
  else if (block.Hardness <= 0.4f)
  {
    groups["crumbly"] = 3;
    groups["oddly_breakable_by_hand"] = 3;
  }
  else if (block.Hardness <= 1.2f)
  {
    groups["cracky"] = 3;
  }
  else if (block.Hardness <= 2.5f)
  {
    groups["cracky"] = 2;
  }
  else
  {
    groups["cracky"] = 1;
  }
  return groups;
}

namespace
{

float StrengthDigMultiplier(const CreatureAttributes &attrs)
{
  return 0.85f + static_cast<float>(attrs.strength) / 40.f;
}

float WearFromUses(int uses, int leveldiff)
{
  if (uses <= 0)
  {
    return 0.f;
  }
  const double real_uses =
      static_cast<double>(uses) * std::pow(3.0, static_cast<double>(leveldiff));
  const double capped = std::min(real_uses, 65535.0);
  if (capped <= 0.0)
  {
    return 0.f;
  }
  return static_cast<float>(1.0 / capped);
}

} // namespace

DigParams ResolveDigParams(const ItemDefinition *tool_or_null,
                           const BlockDefinition &block,
                           const CreatureAttributes &attrs,
                           WorldGameMode mode)
{
  DigParams result;
  if (mode == WorldGameMode::Creative)
  {
    result.Effective = true;
    result.DurationSec = 0.f;
    result.WearDelta = 0.f;
    result.MainGroup = "creative";
    return result;
  }
  if (!(block.Hardness > 0.f))
  {
    result.Effective = false;
    result.DurationSec = -1.f;
    result.WearDelta = 0.f;
    result.MainGroup = "unbreakable";
    return result;
  }

  const ItemDefinition *tool = tool_or_null;
  const auto groups = InferDigGroups(block);
  const int level = groups.count("level") ? groups.at("level") : block.DigLevel;

  float best_time = -1.f;
  float best_wear = 0.f;
  std::string best_group;
  bool found = false;

  if (tool)
  {
    for (const auto &pair : tool->Tool.GroupCaps)
    {
      const std::string &group_name = pair.first;
      const ToolGroupCap &cap = pair.second;
      const auto git = groups.find(group_name);
      if (git == groups.end())
      {
        continue;
      }
      const int leveldiff = cap.MaxLevel - level;
      if (leveldiff < 0)
      {
        continue;
      }
      const int rating = git->second;
      const auto tit = cap.Times.find(rating);
      if (tit == cap.Times.end())
      {
        continue;
      }
      float time = tit->second;
      if (leveldiff > 1)
      {
        time /= static_cast<float>(leveldiff);
      }
      if (!found || time < best_time)
      {
        found = true;
        best_time = time;
        best_wear = WearFromUses(cap.Uses, leveldiff);
        best_group = group_name;
      }
    }
  }

  if (!found)
  {
    // Bare-hand / wrong tool: hardness baseline (block agent DigRules).
    best_time =
        BlockDigRules::DigDurationSeconds(block.Hardness, mode);
    best_wear = 0.f;
    best_group = "hand";
    found = best_time >= 0.f;
  }

  if (!found || best_time < 0.f)
  {
    result.Effective = false;
    result.DurationSec = -1.f;
    return result;
  }

  const float strength_mul = StrengthDigMultiplier(attrs);
  result.Effective = true;
  result.DurationSec = std::max(0.05f, best_time / std::max(0.25f, strength_mul));
  result.WearDelta = best_wear;
  result.MainGroup = best_group;
  return result;
}

bool ApplyItemWear(InventoryEntryRef &entry, const ItemDefinition &def,
                   float wear_delta, bool wear_enabled)
{
  if (!wear_enabled || wear_delta <= 0.f ||
      def.WearEnd == ItemWearEnd::Indestructible)
  {
    return false;
  }
  if (entry.broken)
  {
    return false;
  }
  entry.wear = std::min(1.f, entry.wear + wear_delta);
  if (entry.wear < 1.f)
  {
    return false;
  }
  if (def.WearEnd == ItemWearEnd::Broken)
  {
    entry.broken = true;
    entry.wear = 1.f;
    return false;
  }
  // destroy
  entry = InventoryEntryRef{};
  return true;
}

bool TryRepairItem(InventoryEntryRef &entry, const ItemDefinition &def,
                   const std::string &material_id)
{
  if (entry.empty || entry.kind != InventoryEntryKind::Item)
  {
    return false;
  }
  if (def.Repair.Materials.empty())
  {
    entry.wear = 0.f;
    entry.broken = false;
    return true;
  }
  bool ok = false;
  for (const auto &m : def.Repair.Materials)
  {
    if (m == material_id)
    {
      ok = true;
      break;
    }
  }
  if (!ok)
  {
    return false;
  }
  entry.wear = std::max(0.f, entry.wear - def.Repair.Amount);
  if (entry.wear < 1.f)
  {
    entry.broken = false;
  }
  return true;
}

} // namespace cutum
