#include "World/Objects/ObjectLibrary.h"
#include "Blocks/BlockRegistry.h"
#include "ResourcePacks/BlockNameUtil.h"
#include "ResourcePacks/ResourcePack.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace cutum
{

namespace
{

constexpr const char *kDefaultTag = "misc";

ObjectOrigin OriginFromString(const std::string &value)
{
  if (value == "user")
  {
    return ObjectOrigin::User;
  }
  if (value == "imported")
  {
    return ObjectOrigin::Imported;
  }
  if (value == "builtin")
  {
    return ObjectOrigin::Builtin;
  }
  if (value.rfind("pack:", 0) == 0)
  {
    return ObjectOrigin::ResourcePack;
  }
  return ObjectOrigin::Builtin;
}

} // namespace

void UObjectLibrary::LoadDirectory(const std::filesystem::path &folder,
                                   const std::string &namePrefix,
                                   ObjectOrigin origin, UBlockRegistry &registry)
{
  if (!std::filesystem::exists(folder))
  {
    return;
  }
  for (const auto &entry : std::filesystem::directory_iterator(folder))
  {
    if (!entry.is_regular_file() || entry.path().extension() != ".json")
    {
      continue;
    }
    const std::string localName = entry.path().stem().string();
    const std::string registerName =
        namePrefix.empty() ? localName
                           : MakeQualifiedBlockName(namePrefix, localName);
    LoadFile(entry.path().string(), registry, registerName, origin, namePrefix);
  }
}

void UObjectLibrary::LoadDirectoryRecursive(
    const std::filesystem::path &folder, const std::string &namePrefix,
    ObjectOrigin origin, UBlockRegistry &registry)
{
  if (!std::filesystem::exists(folder))
  {
    return;
  }
  for (const auto &entry : std::filesystem::recursive_directory_iterator(folder))
  {
    if (!entry.is_regular_file() || entry.path().extension() != ".json")
    {
      continue;
    }
    const std::string localName = entry.path().stem().string();
    const std::string registerName =
        namePrefix.empty() ? localName
                           : MakeQualifiedBlockName(namePrefix, localName);
    LoadFile(entry.path().string(), registry, registerName, origin, namePrefix);
  }
}

void UObjectLibrary::LoadMerged(
    const std::filesystem::path &baseFolder,
    const std::vector<ResourcePackManifest> &packs, UBlockRegistry &registry)
{
  Objects.clear();
  LoggedUnknownTypes.clear();
  LoadDirectory(baseFolder, "", ObjectOrigin::Builtin, registry);
  LoadDirectoryRecursive(baseFolder / "imported", "", ObjectOrigin::Imported,
                         registry);
  LoadDirectoryRecursive(baseFolder / "user", "", ObjectOrigin::User, registry);
  for (const auto &pack : packs)
  {
    LoadDirectoryRecursive(pack.Root / "objects", pack.Id,
                           ObjectOrigin::ResourcePack, registry);
  }
  std::cout << "UObjectLibrary: loaded " << Objects.size() << " objects"
            << std::endl;
}

void UObjectLibrary::Load(const std::string &objects_folder,
                          UBlockRegistry &registry)
{
  LoadMerged(std::filesystem::path(objects_folder), {}, registry);
}

bool UObjectLibrary::LoadFile(const std::string &path, UBlockRegistry &registry,
                              const std::string &registerName,
                              ObjectOrigin origin,
                              const std::string &sourcePackId)
{
  std::ifstream file(path);
  if (!file.is_open())
  {
    return false;
  }

  try
  {
    json data = json::parse(file);
    WorldObjectDefinition object;
    object.Name = registerName.empty()
                      ? data.value("name",
                                   std::filesystem::path(path).stem().string())
                      : registerName;
    if (data.contains("tags") && data["tags"].is_array())
    {
      for (const auto &tag : data["tags"])
      {
        if (tag.is_string())
        {
          object.Tags.push_back(tag.get<std::string>());
        }
      }
    }
    if (object.Tags.empty())
    {
      object.Tags.push_back(kDefaultTag);
    }
    object.DisplayName = data.value("displayName", object.Name);
    object.Origin = data.contains("origin")
                        ? OriginFromString(data["origin"].get<std::string>())
                        : origin;
    object.SourcePackId = sourcePackId;
    object.Hidden = data.value("visibility", "") == "hidden";
    if (data.contains("placement") && data["placement"].is_object())
    {
      const json &placement = data["placement"];
      object.PlacementYOffset =
          placement.value("y_offset", object.PlacementYOffset);
      const std::string mode = placement.value("mode", std::string{});
      if (mode == "surface_layer")
      {
        object.PlacementMode = ObjectPlacementMode::SurfaceLayer;
      }
      else if (mode == "vertical_plant")
      {
        object.PlacementMode = ObjectPlacementMode::VerticalPlant;
      }
    }

    if (data.contains("anchor") && data["anchor"].is_array() &&
        data["anchor"].size() == 3)
    {
      object.anchor =
          glm::ivec3(data["anchor"][0].get<int>(), data["anchor"][1].get<int>(),
                     data["anchor"][2].get<int>());
    }

    object.boundsMin = glm::ivec3(std::numeric_limits<int>::max());
    object.boundsMax = glm::ivec3(std::numeric_limits<int>::min());

    for (const auto &blockEntry : data.at("blocks"))
    {
      const int dx = blockEntry.at("dx").get<int>();
      const int dy = blockEntry.at("dy").get<int>();
      const int dz = blockEntry.at("dz").get<int>();
      const std::string type = blockEntry.at("type").get<std::string>();
      const BlockId Id = registry.GetIdByTypeName(type);
      if (Id == BLOCK_AIR)
      {
        const std::string logKey = path + '\x1f' + type;
        if (LoggedUnknownTypes.insert(logKey).second)
        {
          std::cerr << "UObjectLibrary: unknown type '" << type << "' in "
                    << path << std::endl;
        }
        continue;
      }
      ObjectVoxel voxel;
      voxel.offset = glm::ivec3(dx, dy, dz);
      voxel.Id = Id;
      object.voxels.push_back(voxel);

      const glm::ivec3 worldOffset = object.anchor + voxel.offset;
      object.boundsMin = glm::min(object.boundsMin, worldOffset);
      object.boundsMax = glm::max(object.boundsMax, worldOffset);
    }

    if (object.voxels.empty())
    {
      std::cerr << "UObjectLibrary: empty object skipped: " << path << std::endl;
      return false;
    }

    Objects[object.Name] = std::move(object);
    return true;
  }
  catch (const json::exception &e)
  {
    std::cerr << "UObjectLibrary: JSON error in " << path << ": " << e.what()
              << std::endl;
    return false;
  }
}

const WorldObjectDefinition *UObjectLibrary::Get(const std::string &Name) const
{
  const auto it = Objects.find(Name);
  if (it == Objects.end())
  {
    return nullptr;
  }
  return &it->second;
}

std::vector<std::string> UObjectLibrary::ListNames() const
{
  std::vector<std::string> names;
  names.reserve(Objects.size());
  for (const auto &entry : Objects)
  {
    if (!entry.second.Hidden)
    {
      names.push_back(entry.first);
    }
  }
  return names;
}

std::string UObjectLibrary::GetDisplayName(const std::string &Name) const
{
  const WorldObjectDefinition *object = Get(Name);
  if (!object || object->DisplayName.empty())
  {
    return Name;
  }
  return object->DisplayName;
}

std::vector<std::string> UObjectLibrary::GetTags(const std::string &Name) const
{
  const WorldObjectDefinition *object = Get(Name);
  if (!object || object->Tags.empty())
  {
    return {kDefaultTag};
  }
  return object->Tags;
}

ObjectOrigin UObjectLibrary::GetOrigin(const std::string &Name) const
{
  const WorldObjectDefinition *object = Get(Name);
  if (!object)
  {
    return ObjectOrigin::Builtin;
  }
  return object->Origin;
}

} // namespace cutum
