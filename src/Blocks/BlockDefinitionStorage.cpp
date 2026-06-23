#include "Blocks/BlockDefinitionStorage.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace cutum
{

namespace fs = std::filesystem;
using json = nlohmann::json;

void UBlockDefinitionStorage::Load(const std::string &modelsPath)
{
  ById.clear();
  NameToId.clear();
  try
  {
    for (const auto &entry : fs::directory_iterator(modelsPath))
    {
      if (entry.path().extension() != ".json")
      {
        continue;
      }
      std::ifstream file(entry.path());
      if (!file.is_open())
      {
        continue;
      }
      std::stringstream buffer;
      buffer << file.rdbuf();
      json d;
      try
      {
        d = json::parse(buffer.str());
      }
      catch (const json::exception &e)
      {
        std::cerr << "UBlockDefinitionStorage: parse error " << entry.path()
                  << ": " << e.what() << std::endl;
        continue;
      }
      BlockDefinition def;
      def.Name = d.value("name", "");
      def.Id = static_cast<BlockId>(d.value("id", 0));
      if (def.Name.empty() || def.Id == BLOCK_AIR)
      {
        continue;
      }
      if (d.contains("animation"))
      {
        def.Animation = ParseAnimationFromJson(d["animation"]);
      }
      if (d.contains("physics"))
      {
        def.Physics = ParsePhysicsFromJson(d["physics"]);
      }
      else
      {
        def.Physics = BlockPhysicsProfile::Solid();
      }
      if (d.contains("render"))
      {
        def.Render = ParseRenderFromJson(d["render"]);
      }
      if (d.contains("types") && d["types"].is_array())
      {
        for (const auto &t : d["types"])
        {
          if (t.is_string())
          {
            def.Types.push_back(t.get<std::string>());
          }
        }
      }
      if (d.contains("physics") && d["physics"].is_object() &&
          d["physics"].contains("preset") && d["physics"]["preset"].is_string())
      {
        ApplyRenderPresetDefaults(def.Render,
                                  d["physics"]["preset"].get<std::string>());
      }
      ById[def.Id] = def;
      NameToId[def.Name] = def.Id;
    }
  }
  catch (const fs::filesystem_error &ex)
  {
    std::cerr << "UBlockDefinitionStorage::Load: " << ex.what() << std::endl;
  }
}

const BlockDefinition *UBlockDefinitionStorage::GetById(BlockId Id) const
{
  const auto it = ById.find(Id);
  if (it != ById.end())
  {
    return &it->second;
  }
  return nullptr;
}

const BlockDefinition *
UBlockDefinitionStorage::GetByName(const std::string &Name) const
{
  const auto it = NameToId.find(Name);
  if (it == NameToId.end())
  {
    return nullptr;
  }
  return GetById(it->second);
}

void UBlockDefinitionStorage::ReplaceAll(
    std::unordered_map<BlockId, BlockDefinition> newById,
    std::unordered_map<std::string, BlockId> newNameToId)
{
  ById = std::move(newById);
  NameToId = std::move(newNameToId);
}

} // namespace cutum
