#include "Creatures/Definition/SkinDefinitionStorage.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <glm/glm.hpp>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

namespace
{

glm::vec4 ReadVec4(const nlohmann::json &arr, const glm::vec4 &fallback)
{
  if (!arr.is_array() || arr.size() < 4)
  {
    return fallback;
  }
  return glm::vec4(arr[0].get<float>(), arr[1].get<float>(),
                   arr[2].get<float>(), arr[3].get<float>());
}

} // namespace

void USkinDefinitionStorage::Load(const std::string &folder)
{
  definitions_.clear();
  if (!std::filesystem::exists(folder))
  {
    return;
  }
  for (const auto &entry : std::filesystem::directory_iterator(folder))
  {
    if (!entry.is_directory())
    {
      continue;
    }
    const std::filesystem::path jsonPath = entry.path() / "skin.json";
    if (std::filesystem::exists(jsonPath))
    {
      LoadFile(jsonPath.string());
    }
  }
  std::cout << "USkinDefinitionStorage: loaded " << definitions_.size()
            << " skins" << std::endl;
}

bool USkinDefinitionStorage::LoadFile(const std::string &path)
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
    SkinDefinition def;
    def.id = data.value("id", "");
    if (def.id.empty())
    {
      return false;
    }
    def.displayName = data.value("display_name", def.id);
    def.creatureId = data.value("creature_id", "");
    if (data.contains("catalog") && data["catalog"].is_object())
    {
      const auto &catalog = data["catalog"];
      if (catalog.contains("tags") && catalog["tags"].is_array())
      {
        for (const auto &tag : catalog["tags"])
        {
          if (tag.is_string())
          {
            def.catalog.tags.push_back(tag.get<std::string>());
          }
        }
      }
      def.catalog.equippable = catalog.value("equippable", true);
      def.catalog.sortOrder = catalog.value("sort_order", 0);
    }
    if (data.contains("visual") && data["visual"].is_object())
    {
      const auto &vis = data["visual"];
      def.textureKey = vis.value("texture", def.textureKey);
      if (vis.contains("texture_map") && vis["texture_map"].is_object())
      {
        for (auto it = vis["texture_map"].begin();
             it != vis["texture_map"].end(); ++it)
        {
          if (it.value().is_string())
          {
            def.textureMap[it.key()] = it.value().get<std::string>();
          }
        }
      }
      def.wireframeTint =
          ReadVec4(vis.value("wireframe_color", nlohmann::json::array()),
                   def.wireframeTint);
      if (vis.contains("icon") && vis["icon"].is_object())
      {
        const auto &icon = vis["icon"];
        def.iconMode = icon.value("mode", def.iconMode);
        def.iconFallbackColor =
            ReadVec4(icon.value("fallback_color", nlohmann::json::array()),
                     def.iconFallbackColor);
      }
    }
    definitions_[def.id] = def;
    return true;
  }
  catch (const std::exception &e)
  {
    std::cerr << "USkinDefinitionStorage: " << path << ": " << e.what()
              << std::endl;
    return false;
  }
}

const SkinDefinition *USkinDefinitionStorage::Get(const std::string &id) const
{
  const auto it = definitions_.find(id);
  if (it == definitions_.end())
  {
    return nullptr;
  }
  return &it->second;
}

std::vector<std::string> USkinDefinitionStorage::ListEquippable() const
{
  std::vector<std::string> ids;
  for (const auto &[id, def] : definitions_)
  {
    if (def.catalog.equippable)
    {
      ids.push_back(id);
    }
  }
  std::sort(ids.begin(), ids.end(),
            [this](const std::string &a, const std::string &b)
            {
              const auto *defA = Get(a);
              const auto *defB = Get(b);
              const int orderA = defA ? defA->catalog.sortOrder : 0;
              const int orderB = defB ? defB->catalog.sortOrder : 0;
              if (orderA != orderB)
              {
                return orderA < orderB;
              }
              return a < b;
            });
  return ids;
}

bool USkinDefinitionStorage::IsCompatible(const std::string &skinId,
                                          const std::string &speciesId) const
{
  const SkinDefinition *skin = Get(skinId);
  if (!skin)
  {
    return false;
  }
  return skin->creatureId == speciesId;
}

} // namespace cutum
