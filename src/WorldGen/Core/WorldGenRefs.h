#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cutum
{

struct WorldGenSlotSpec
{
  std::vector<std::string> BlockNames;
  std::string FallbackSlot;
};

using WorldGenRefsCatalog = std::unordered_map<std::string, WorldGenSlotSpec>;

class UWorldGenRefs
{
public:
  static bool LoadFromFile(const std::filesystem::path &path);
  static const WorldGenSlotSpec *GetSlot(const std::string &slotName);
  static std::shared_ptr<const WorldGenRefsCatalog> GetSnapshot();
  static bool IsLoaded();

private:
  static std::shared_ptr<const WorldGenRefsCatalog> Active;
};

} // namespace cutum
