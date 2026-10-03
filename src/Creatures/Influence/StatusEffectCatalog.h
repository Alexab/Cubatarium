#ifndef STATUS_EFFECT_CATALOG_H
#define STATUS_EFFECT_CATALOG_H

#include "Creatures/Influence/StatusEffectTypes.h"
#include <string>
#include <unordered_map>

namespace cutum
{

class UStatusEffectCatalog
{
public:
  static UStatusEffectCatalog &Get();

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
