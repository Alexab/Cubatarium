#ifndef IU_CHARACTER_STATS_VIEW_MODEL_H
#define IU_CHARACTER_STATS_VIEW_MODEL_H

#include "Creatures/Stats/CreatureAttributes.h"
#include "Creatures/Stats/CreatureVitals.h"
#include "Game/WorldGameMode.h"
#include <string>

namespace cutum
{

struct CharacterStatsSnapshot
{
  std::string displayName;
  std::string typeId;
  std::string skinId;
  CreatureVitals vitals{};
  CreatureAttributes attributes{};
  WorldGameMode gameMode{WorldGameMode::Creative};
  bool valid{false};
};

class IUCharacterStatsViewModel
{
public:
  virtual ~IUCharacterStatsViewModel() = default;
  virtual CharacterStatsSnapshot GetCharacterStatsSnapshot() const = 0;
  virtual WorldGameMode GetWorldGameMode() const = 0;
};

} // namespace cutum

#endif
