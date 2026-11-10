#include "Creatures/Influence/StatusEffectCatalog.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

namespace
{

StatusStackPolicy ParseStackPolicy(const std::string &raw)
{
  if (raw == "stack")
  {
    return StatusStackPolicy::Stack;
  }
  if (raw == "ignore_if_present" || raw == "ignore")
  {
    return StatusStackPolicy::IgnoreIfPresent;
  }
  return StatusStackPolicy::Refresh;
}

bool ParseEffectFile(const std::filesystem::path &path, StatusEffectDef &out)
{
  try
  {
    std::ifstream in(path);
    if (!in)
    {
      return false;
    }
    nlohmann::json j;
    in >> j;
    out.Id = j.value("id", path.stem().string());
    if (out.Id.empty())
    {
      return false;
    }
    out.DurationSec = j.value("duration_sec", out.DurationSec);
    out.TickIntervalSec = j.value("tick_interval_sec", out.TickIntervalSec);
    out.HealthPerTick = j.value("health_per_tick", out.HealthPerTick);
    out.MoveSpeedMul = j.value("move_speed_mul", out.MoveSpeedMul);
    out.StrengthDelta = j.value("strength_delta", out.StrengthDelta);
    out.AgilityDelta = j.value("agility_delta", out.AgilityDelta);
    out.MaxStacks = j.value("max_stacks", out.MaxStacks);
    if (j.contains("stack") && j["stack"].is_string())
    {
      out.Stack = ParseStackPolicy(j["stack"].get<std::string>());
    }
    return true;
  }
  catch (const std::exception &ex)
  {
    std::cerr << "StatusEffectCatalog: skip " << path.string() << ": "
              << ex.what() << "\n";
    return false;
  }
}

} // namespace

UStatusEffectCatalog &UStatusEffectCatalog::Get()
{
  static UStatusEffectCatalog catalog;
  catalog.EnsureBuiltins();
  return catalog;
}

void UStatusEffectCatalog::TryLoadFromModelsEffects(const std::string &dir)
{
  if (JsonLoadAttempted)
  {
    return;
  }
  JsonLoadAttempted = true;
  namespace fs = std::filesystem;
  const fs::path root(dir);
  if (!fs::is_directory(root))
  {
    return;
  }
  for (const auto &entry : fs::directory_iterator(root))
  {
    if (!entry.is_regular_file() || entry.path().extension() != ".json")
    {
      continue;
    }
    StatusEffectDef def;
    if (ParseEffectFile(entry.path(), def))
    {
      Register(def);
    }
  }
}

void UStatusEffectCatalog::EnsureBuiltins()
{
  TryLoadFromModelsEffects();
  if (BuiltinsReady)
  {
    return;
  }
  if (!Find("bleed"))
  {
    StatusEffectDef bleed;
    bleed.Id = "bleed";
    bleed.DurationSec = 4.f;
    bleed.TickIntervalSec = 1.f;
    bleed.HealthPerTick = -2.f;
    bleed.Stack = StatusStackPolicy::Refresh;
    Register(bleed);
  }
  if (!Find("slow"))
  {
    StatusEffectDef slow;
    slow.Id = "slow";
    slow.DurationSec = 3.f;
    slow.TickIntervalSec = 0.f;
    slow.MoveSpeedMul = 0.6f;
    slow.AgilityDelta = -2;
    slow.Stack = StatusStackPolicy::Refresh;
    Register(slow);
  }
  BuiltinsReady = true;
}

void UStatusEffectCatalog::Register(const StatusEffectDef &def)
{
  if (def.Id.empty())
  {
    return;
  }
  ById[def.Id] = def;
}

const StatusEffectDef *UStatusEffectCatalog::Find(const std::string &id) const
{
  const auto it = ById.find(id);
  return it == ById.end() ? nullptr : &it->second;
}

} // namespace cutum
