#ifndef SKINDEFINITIONSTORAGE_H
#define SKINDEFINITIONSTORAGE_H

#include "Creatures/Definition/SkinDefinition.h"
#include <atomic>
#include <shared_mutex>
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
  size_t Count() const;

  std::vector<std::string> ListEquippable() const;
  bool IsCompatible(const std::string &skinId,
                    const std::string &speciesId) const;

  /// Bumped on every Load/LoadOverlay/LoadFile; consumers cache resolved
  /// data keyed on it (creature appearance cache, audit V6).
  std::atomic<uint64_t> Revision{1};

private:
  bool LoadFile(const std::string &path);

  std::unordered_map<std::string, SkinDefinition> Definitions;
  mutable std::shared_mutex DefinitionsMutex;
};

} // namespace cutum

#endif
