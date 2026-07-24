#ifndef CREATUREDEFINITIONSTORAGE_H
#define CREATUREDEFINITIONSTORAGE_H

#include "Creatures/Definition/CreatureDefinition.h"
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace cutum
{

class UCreatureDefinitionStorage
{
public:
  void Load(const std::string &folder);
  void LoadOverlay(const std::string &folder);
  const CreatureDefinition *Get(const std::string &Id) const;
  size_t Count() const;

  std::vector<std::string> ListAllIds() const;
  std::vector<std::string> ListSpawnable() const;
  std::string GetControlledDefaultSpeciesId() const;

private:
  bool LoadFile(const std::string &path);

  std::unordered_map<std::string, CreatureDefinition> Definitions;
  mutable std::shared_mutex DefinitionsMutex;
};

} // namespace cutum

#endif
