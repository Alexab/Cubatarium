#pragma once

#include <filesystem>
#include <memory>
#include <nlohmann/json_fwd.hpp>
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

std::shared_ptr<const WorldGenRefsCatalog> CompileWorldGenRefsCatalog(const nlohmann::json &root);

class UWorldGenRefs
{
public:
  static bool LoadFromFile(const std::filesystem::path &path);
  static const WorldGenSlotSpec *GetSlot(const std::string &slotName);
  static std::shared_ptr<const WorldGenRefsCatalog> GetSnapshot();
  static std::shared_ptr<const WorldGenRefsCatalog> GetThreadReadSnapshot();
  static void ReleaseThreadRead() noexcept;
  static bool IsLoaded();

private:
  static std::shared_ptr<const WorldGenRefsCatalog> &ThreadReadKeep();
  static std::shared_ptr<const WorldGenRefsCatalog> Active;
};

} // namespace cutum
