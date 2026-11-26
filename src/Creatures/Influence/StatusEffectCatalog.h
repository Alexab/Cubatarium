#ifndef STATUS_EFFECT_CATALOG_H
#define STATUS_EFFECT_CATALOG_H

#include "Creatures/Influence/StatusEffectTypes.h"
#include <string>
#include <unordered_map>

namespace cutum
{

struct StatusEffectCatalogStorage
{
  size_t inlineBytes{}, tableBytes{}, definitions{};
  bool published{}, filesAttempted{}, builtinsReady{}, supported{};
};

class UStatusEffectCatalog
{
public:
  static UStatusEffectCatalog &Get();
  static UStatusEffectCatalog LoadFromDirectory(const std::string &dir);
  static StatusEffectCatalogStorage InspectPublishedStorage() noexcept;
  StatusEffectCatalogStorage InspectOwnedStorage() const noexcept;

  /// Load `models/effects/*.json` then fill any missing builtins (bleed/slow).
  void EnsureBuiltins();
  void Register(const StatusEffectDef &def);
  const StatusEffectDef *Find(const std::string &id) const;

  /// Explicit load path (tests); also invoked from EnsureBuiltins.
  void TryLoadFromModelsEffects(const std::string &dir = "models/effects");

private:
  UStatusEffectCatalog() = default;
  std::unordered_map<std::string, StatusEffectDef> ById;
  bool BuiltinsReady{false};
  bool JsonLoadAttempted{false};
};

} // namespace cutum

#endif
