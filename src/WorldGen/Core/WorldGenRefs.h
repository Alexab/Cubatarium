#pragma once

#include <filesystem>
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

class UWorldGenRefs
{
public:
  static bool LoadFromFile(const std::filesystem::path &path);
  static const WorldGenSlotSpec *GetSlot(const std::string &slotName);
  static bool IsLoaded();

private:
  static std::unordered_map<std::string, WorldGenSlotSpec> Slots;
};

} // namespace cutum
