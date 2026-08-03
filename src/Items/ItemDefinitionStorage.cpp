#include "Items/ItemDefinitionStorage.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

namespace
{

ToolGroupCap ParseGroupCap(const nlohmann::json &j)
{
  ToolGroupCap cap;
  cap.MaxLevel = j.value("maxlevel", 1);
  cap.Uses = j.value("uses", 20);
  if (j.contains("times") && j["times"].is_object())
  {
    for (auto it = j["times"].begin(); it != j["times"].end(); ++it)
    {
      try
      {
        const int rating = std::stoi(it.key());
        if (it.value().is_number())
        {
          cap.Times[rating] = it.value().get<float>();
        }
      }
      catch (...)
      {
      }
    }
  }
  return cap;
}

} // namespace

void UItemDefinitionStorage::EnsureHandDefinition()
{
  if (Definitions.count("hand"))
  {
    return;
  }
  ItemDefinition hand;
  hand.Id = "hand";
  hand.DisplayName = "Hand";
  hand.StackMax = 1;
  hand.WearEnd = ItemWearEnd::Indestructible;
  hand.HandFallback = true;
  hand.Hidden = true;
  hand.Tool.FullPunchInterval = 1.0f;
  hand.Tool.PunchAttackUses = 0;
  hand.Tool.Damage.Melee = 1.f;
  hand.Tool.Damage.Groups["fleshy"] = 1;
  ToolGroupCap soft;
  soft.MaxLevel = 1;
  soft.Uses = 0;
  soft.Times[3] = 0.8f;
  soft.Times[2] = 1.5f;
  hand.Tool.GroupCaps["oddly_breakable_by_hand"] = soft;
  ToolGroupCap crumbly;
  crumbly.MaxLevel = 1;
  crumbly.Uses = 0;
  crumbly.Times[3] = 1.0f;
  crumbly.Times[2] = 1.8f;
  hand.Tool.GroupCaps["crumbly"] = crumbly;
  Definitions["hand"] = std::move(hand);
}

void UItemDefinitionStorage::Clear()
{
  std::unique_lock lock(DefinitionsMutex);
  Definitions.clear();
  EnsureHandDefinition();
}

void UItemDefinitionStorage::Load(const std::string &folder)
{
  {
    std::unique_lock lock(DefinitionsMutex);
    Definitions.clear();
    EnsureHandDefinition();
  }
  if (!std::filesystem::exists(folder))
  {
    std::cout << "UItemDefinitionStorage: folder missing " << folder
              << std::endl;
    return;
  }
  size_t loaded = 0;
  for (const auto &entry :
       std::filesystem::directory_iterator(folder))
  {
    if (!entry.is_regular_file())
    {
      continue;
    }
    if (entry.path().extension() != ".json")
    {
      continue;
    }
    if (LoadFile(entry.path().string()))
    {
      ++loaded;
    }
  }
  std::cout << "UItemDefinitionStorage: loaded " << loaded << " items from "
            << folder << std::endl;
}

void UItemDefinitionStorage::LoadOverlay(const std::string &folder)
{
  if (!std::filesystem::exists(folder))
  {
    return;
  }
  size_t overlay_count = 0;
  for (const auto &entry :
       std::filesystem::directory_iterator(folder))
  {
    if (!entry.is_regular_file() || entry.path().extension() != ".json")
    {
      continue;
    }
    if (LoadFile(entry.path().string()))
    {
      ++overlay_count;
    }
  }
  if (overlay_count > 0)
  {
    std::cout << "UItemDefinitionStorage: applied " << overlay_count
              << " item overlay(s)" << std::endl;
  }
}

bool UItemDefinitionStorage::LoadFile(const std::string &path)
{
  try
  {
    std::ifstream file(path);
    if (!file.is_open())
    {
      return false;
    }
    nlohmann::json data;
    file >> data;
    ItemDefinition def;
    def.Id = data.value("id", "");
    if (def.Id.empty())
    {
      def.Id = data.value("name", "");
    }
    if (def.Id.empty())
    {
      return false;
    }
    def.DisplayName = data.value("displayName", def.Id);
    if (data.contains("display_name"))
    {
      def.DisplayName = data.value("display_name", def.DisplayName);
    }
    def.StackMax = data.value("stack_max", 1);
    def.WearEnd =
        ItemWearEndFromString(data.value("wear_end", std::string("destroy")));
    def.ModelPath = data.value("model", "");
    def.HandFallback = data.value("hand_fallback", false);
    def.Hidden = data.value("hidden", false);
    if (data.contains("types") && data["types"].is_array())
    {
      for (const auto &t : data["types"])
      {
        if (t.is_string())
        {
          def.Types.push_back(t.get<std::string>());
        }
      }
    }
    if (data.contains("repair") && data["repair"].is_object())
    {
      const auto &r = data["repair"];
      def.Repair.Amount = r.value("amount", 0.25f);
      if (r.contains("materials") && r["materials"].is_array())
      {
        for (const auto &m : r["materials"])
        {
          if (m.is_string())
          {
            def.Repair.Materials.push_back(m.get<std::string>());
          }
        }
      }
    }
    if (data.contains("tool") && data["tool"].is_object())
    {
      const auto &tool = data["tool"];
      def.Tool.FullPunchInterval = tool.value("full_punch_interval", 1.0f);
      def.Tool.PunchAttackUses = tool.value("punch_attack_uses", 0);
      if (tool.contains("damage") && tool["damage"].is_object())
      {
        const auto &dmg = tool["damage"];
        def.Tool.Damage.Melee = dmg.value("melee", 0.f);
        for (auto it = dmg.begin(); it != dmg.end(); ++it)
        {
          if (it.key() == "melee")
          {
            continue;
          }
          if (it.value().is_number_integer() || it.value().is_number_unsigned())
          {
            def.Tool.Damage.Groups[it.key()] = it.value().get<int>();
          }
          else if (it.value().is_number_float())
          {
            def.Tool.Damage.Groups[it.key()] =
                static_cast<int>(std::lround(it.value().get<float>()));
          }
        }
        if (def.Tool.Damage.Groups.find("fleshy") ==
                def.Tool.Damage.Groups.end() &&
            def.Tool.Damage.Melee > 0.f)
        {
          def.Tool.Damage.Groups["fleshy"] = std::max(
              1, static_cast<int>(std::lround(def.Tool.Damage.Melee)));
        }
      }
      if (tool.contains("groupcaps") && tool["groupcaps"].is_object())
      {
        for (auto it = tool["groupcaps"].begin(); it != tool["groupcaps"].end();
             ++it)
        {
          if (it.value().is_object())
          {
            def.Tool.GroupCaps[it.key()] = ParseGroupCap(it.value());
          }
        }
      }
    }
    {
      std::unique_lock lock(DefinitionsMutex);
      Definitions[def.Id] = std::move(def);
    }
    return true;
  }
  catch (const std::exception &e)
  {
    std::cerr << "UItemDefinitionStorage: " << path << ": " << e.what()
              << std::endl;
    return false;
  }
}

const ItemDefinition *UItemDefinitionStorage::Get(const std::string &Id) const
{
  std::shared_lock lock(DefinitionsMutex);
  const auto it = Definitions.find(Id);
  if (it == Definitions.end())
  {
    return nullptr;
  }
  return &it->second;
}

size_t UItemDefinitionStorage::Count() const
{
  std::shared_lock lock(DefinitionsMutex);
  return Definitions.size();
}

std::vector<std::string> UItemDefinitionStorage::ListIds() const
{
  std::shared_lock lock(DefinitionsMutex);
  std::vector<std::string> ids;
  ids.reserve(Definitions.size());
  for (const auto &pair : Definitions)
  {
    ids.push_back(pair.first);
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

std::vector<std::string> UItemDefinitionStorage::ListCatalogIds() const
{
  std::shared_lock lock(DefinitionsMutex);
  std::vector<std::string> ids;
  for (const auto &pair : Definitions)
  {
    if (!pair.second.Hidden)
    {
      ids.push_back(pair.first);
    }
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

std::vector<std::string>
UItemDefinitionStorage::GetTypes(const std::string &Id) const
{
  std::shared_lock lock(DefinitionsMutex);
  const auto it = Definitions.find(Id);
  if (it == Definitions.end())
  {
    return {};
  }
  return it->second.Types;
}

std::string UItemDefinitionStorage::GetDisplayName(const std::string &Id) const
{
  std::shared_lock lock(DefinitionsMutex);
  const auto it = Definitions.find(Id);
  if (it == Definitions.end())
  {
    return Id;
  }
  return it->second.DisplayName;
}

const ItemDefinition *UItemDefinitionStorage::GetHandDefinition() const
{
  return Get("hand");
}

} // namespace cutum
