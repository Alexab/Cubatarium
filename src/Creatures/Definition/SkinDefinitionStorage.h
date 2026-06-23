#ifndef SKINDEFINITIONSTORAGE_H
#define SKINDEFINITIONSTORAGE_H

#include "Creatures/Definition/SkinDefinition.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace cutum
{

class USkinDefinitionStorage
{
public:
  void Load(const std::string &folder);
  void LoadOverlay(const std::string &folder);
  const SkinDefinition *Get(const std::string &Id) const;
  size_t Count() const { return Definitions.size(); }

  std::vector<std::string> ListEquippable() const;
  bool IsCompatible(const std::string &skinId,
                    const std::string &speciesId) const;

private:
  bool LoadFile(const std::string &path);

  std::unordered_map<std::string, SkinDefinition> Definitions;
};

} // namespace cutum

#endif
