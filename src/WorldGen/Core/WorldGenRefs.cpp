#include "WorldGen/Core/WorldGenRefs.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

std::unordered_map<std::string, WorldGenSlotSpec> UWorldGenRefs::Slots;

bool UWorldGenRefs::LoadFromFile(const std::filesystem::path &path)
{
  Slots.clear();
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
      Slots[slotName] = std::move(spec);
    }
  }
  catch (const nlohmann::json::exception &e)
  {
    std::cerr << "WorldGenRefs: parse error " << path << ": " << e.what()
              << std::endl;
    Slots.clear();
    return false;
  }
  return !Slots.empty();
}

const WorldGenSlotSpec *UWorldGenRefs::GetSlot(const std::string &slotName)
{
  const auto it = Slots.find(slotName);
  if (it == Slots.end())
  {
    return nullptr;
  }
  return &it->second;
}

bool UWorldGenRefs::IsLoaded() { return !Slots.empty(); }

} // namespace cutum
