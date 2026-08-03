#ifndef STATUS_EFFECT_CATALOG_H
#define STATUS_EFFECT_CATALOG_H

#include "Creatures/Influence/StatusEffectTypes.h"
#include <optional>
#include <string>
#include <unordered_map>

namespace cutum
{

class UStatusEffectCatalog
{
public:
  static UStatusEffectCatalog &Get();

  void EnsureBuiltins();
  void Register(const StatusEffectDef &def);
  const StatusEffectDef *Find(const std::string &id) const;

private:
  UStatusEffectCatalog() = default;
  std::unordered_map<std::string, StatusEffectDef> ById;
  bool BuiltinsReady{false};
};

} // namespace cutum

#endif
