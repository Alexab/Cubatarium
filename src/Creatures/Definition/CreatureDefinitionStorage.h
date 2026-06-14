#ifndef CREATUREDEFINITIONSTORAGE_H
#define CREATUREDEFINITIONSTORAGE_H

#include "Creatures/Definition/CreatureDefinition.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace cutum
{

class UCreatureDefinitionStorage
{
public:
  void Load(const std::string &folder);
  const CreatureDefinition *Get(const std::string &id) const;
  size_t Count() const { return definitions_.size(); }

  std::vector<std::string> ListAllIds() const;
  std::vector<std::string> ListSpawnable() const;
  std::string GetControlledDefaultSpeciesId() const;

private:
  bool LoadFile(const std::string &path);

  std::unordered_map<std::string, CreatureDefinition> definitions_;
};

} // namespace cutum

#endif
