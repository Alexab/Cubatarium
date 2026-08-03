#ifndef ITEM_DEFINITION_STORAGE_H
#define ITEM_DEFINITION_STORAGE_H

#include "Items/ItemDefinition.h"
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace cutum
{

class UItemDefinitionStorage
{
public:
  void Load(const std::string &folder);
  void LoadOverlay(const std::string &folder);
  void Clear();

  const ItemDefinition *Get(const std::string &Id) const;
  size_t Count() const;
  std::vector<std::string> ListIds() const;
  std::vector<std::string> ListCatalogIds() const;
  std::vector<std::string> GetTypes(const std::string &Id) const;
  std::string GetDisplayName(const std::string &Id) const;

  const ItemDefinition *GetHandDefinition() const;

private:
  bool LoadFile(const std::string &path);
  void EnsureHandDefinition();

  std::unordered_map<std::string, ItemDefinition> Definitions;
  mutable std::shared_mutex DefinitionsMutex;
};

} // namespace cutum

#endif
