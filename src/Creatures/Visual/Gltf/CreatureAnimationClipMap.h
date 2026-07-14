#ifndef CREATUREANIMATIONCLIPMAP_H
#define CREATUREANIMATIONCLIPMAP_H

#include "Creatures/Locomotion/LocomotionTypes.h"
#include <optional>
#include <string>
#include <unordered_map>

namespace cutum
{

inline std::optional<std::string>
ResolveAnimationClipId(LocomotionState state,
                       const std::unordered_map<std::string, std::string> &stateMap)
{
  const char *keys[] = {LocomotionStateCatalogKey(state), ToString(state)};
  for (const char *stateName : keys)
  {
    if (stateName == nullptr || stateName[0] == '\0')
    {
      continue;
    }
    const auto it = stateMap.find(stateName);
    if (it != stateMap.end())
    {
      return it->second;
    }
  }
  return std::nullopt;
}

} // namespace cutum

#endif
