#include "WorldGen/Core/WorldGenRefs.h"
#include "WorldGen/Core/WorldGenContentPin.h"
#include "WorldGen/Core/WorldGenContentPinTls.h"
#include <atomic>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

std::shared_ptr<const WorldGenRefsCatalog> UWorldGenRefs::Active =
    std::make_shared<WorldGenRefsCatalog>();

bool UWorldGenRefs::LoadFromFile(const std::filesystem::path &path)
{
  auto next = std::make_shared<WorldGenRefsCatalog>();
  std::ifstream in(path);
  if (!in.is_open())
  {
    std::cerr << "WorldGenRefs: could not open " << path << std::endl;
    return false;
  }
  try
  {
    const nlohmann::json root = nlohmann::json::parse(in);
    if (!root.contains("slots") || !root["slots"].is_object())
    {
      std::cerr << "WorldGenRefs: missing slots object in " << path << std::endl;
      return false;
    }
    for (const auto &[slotName, slotVal] : root["slots"].items())
    {
      if (!slotVal.is_object())
      {
        continue;
      }
      WorldGenSlotSpec spec;
      if (slotVal.contains("block_names") && slotVal["block_names"].is_array())
      {
        for (const auto &name : slotVal["block_names"])
        {
          if (name.is_string())
          {
            spec.BlockNames.push_back(name.get<std::string>());
          }
        }
      }
      if (spec.BlockNames.empty())
      {
        spec.BlockNames.push_back(slotName);
      }
      if (slotVal.contains("fallback_slot") && slotVal["fallback_slot"].is_string())
      {
        spec.FallbackSlot = slotVal["fallback_slot"].get<std::string>();
      }
      (*next)[slotName] = std::move(spec);
    }
  }
  catch (const nlohmann::json::exception &e)
  {
    std::cerr << "WorldGenRefs: parse error " << path << ": " << e.what()
              << std::endl;
    return false;
  }
  if (next->empty())
  {
    return false;
  }
  std::atomic_store_explicit(&Active, std::shared_ptr<const WorldGenRefsCatalog>(
                                          std::move(next)),
                              std::memory_order_release);
  return true;
}

std::shared_ptr<const WorldGenRefsCatalog> UWorldGenRefs::GetSnapshot()
{
  return std::atomic_load_explicit(&Active, std::memory_order_acquire);
}

const WorldGenSlotSpec *UWorldGenRefs::GetSlot(const std::string &slotName)
{
  if (const WorldGenContentSnapshot *pin = GetPinnedWorldGenContent())
  {
    if (pin->Refs)
    {
      const auto it = pin->Refs->find(slotName);
      if (it == pin->Refs->end())
      {
        return nullptr;
      }
      return &it->second;
    }
  }
  thread_local std::shared_ptr<const WorldGenRefsCatalog> keep;
  keep = GetSnapshot();
  const auto it = keep->find(slotName);
  if (it == keep->end())
  {
    return nullptr;
  }
  return &it->second;
}

bool UWorldGenRefs::IsLoaded()
{
  const auto snap = GetSnapshot();
  return snap && !snap->empty();
}

} // namespace cutum
