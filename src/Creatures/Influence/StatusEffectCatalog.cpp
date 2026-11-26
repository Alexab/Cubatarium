#include "Creatures/Influence/StatusEffectCatalog.h"
#include "Core/Memory/StlSourceStorage.h"
#include "WorldGen/Core/CatalogJsonSource.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

namespace
{
std::atomic<const UStatusEffectCatalog *> PublishedCatalog{nullptr};

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
    const auto j = ParseAndCloseCatalogFileJson(in, CatalogJsonKind::StatusEffects);
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
  catch (const std::bad_alloc &)
  {
    throw;
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
  struct Owner
  {
    UStatusEffectCatalog Catalog = LoadFromDirectory("models/effects");
    Owner() { PublishedCatalog.store(&Catalog, std::memory_order_release); }
    ~Owner() { PublishedCatalog.store(nullptr, std::memory_order_release); }
  };
  static Owner owner;
  return owner.Catalog;
}

UStatusEffectCatalog UStatusEffectCatalog::LoadFromDirectory(const std::string &dir)
{
  UStatusEffectCatalog catalog;
  catalog.TryLoadFromModelsEffects(dir);
  catalog.EnsureBuiltins();
  return catalog;
}

StatusEffectCatalogStorage UStatusEffectCatalog::InspectPublishedStorage() noexcept
{
  const auto *catalog = PublishedCatalog.load(std::memory_order_acquire);
  auto out = catalog ? catalog->InspectOwnedStorage() : StatusEffectCatalogStorage{};
  out.published = catalog != nullptr;
  out.inlineBytes = sizeof(UStatusEffectCatalog) + sizeof(PublishedCatalog);
  if (!catalog)
    out.supported = StlSourceStorage::SupportedProfile();
  return out;
}

StatusEffectCatalogStorage UStatusEffectCatalog::InspectOwnedStorage() const noexcept
{
  StlSourceStorage table;
  table.Hash(ById);
  for (const auto &[id, def] : ById)
  {
    table.Text(id);
    table.Text(def.Id);
    table.bytes.supported &= !id.empty() && id == def.Id;
  }
  return {sizeof(*this), table.bytes.requested, ById.size(), false,
          JsonLoadAttempted, BuiltinsReady, table.bytes.supported};
}

void UStatusEffectCatalog::TryLoadFromModelsEffects(const std::string &dir)
{
  if (JsonLoadAttempted)
  {
    return;
  }
  namespace fs = std::filesystem;
  const fs::path root(dir);
  if (fs::is_directory(root))
  {
    for (const auto &entry : fs::directory_iterator(root))
    {
      if (!entry.is_regular_file() || entry.path().extension() != ".json")
        continue;
      StatusEffectDef def;
      if (ParseEffectFile(entry.path(), def))
        Register(def);
    }
  }
  JsonLoadAttempted = true;
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
  StatusEffectDef next = def;
  ById.insert_or_assign(def.Id, std::move(next));
}

const StatusEffectDef *UStatusEffectCatalog::Find(const std::string &id) const
{
  const auto it = ById.find(id);
  return it == ById.end() ? nullptr : &it->second;
}

} // namespace cutum
