#ifndef IU_CHARACTER_STATS_VIEW_MODEL_H
#define IU_CHARACTER_STATS_VIEW_MODEL_H

#include "Creatures/Stats/CreatureAttributes.h"
#include "Creatures/Stats/CreatureVitals.h"
#include "Game/WorldDifficulty.h"
#include "Game/WorldGameMode.h"
#include <array>
#include <string>

namespace cutum
{

struct CharacterStatsSnapshot
{
  struct EquippedSlotSnapshot
  {
    std::string itemId;
    float wear{0.f};
    bool broken{false};
    bool isBlock{false};
  };

  std::string displayName;
  std::string typeId;
  std::string skinId;
  CreatureVitals vitals{};
  CreatureAttributes attributes{};
  std::array<EquippedSlotSnapshot, 6> equippedArmor{};
  std::array<EquippedSlotSnapshot, 2> equippedTools{};
  WorldGameMode gameMode{WorldGameMode::Creative};
  WorldDifficulty difficulty{WorldDifficulty::Normal};
  bool valid{false};
};

class IUCharacterStatsViewModel
{
public:
  virtual ~IUCharacterStatsViewModel() = default;
  virtual CharacterStatsSnapshot GetCharacterStatsSnapshot() const = 0;
  virtual WorldGameMode GetWorldGameMode() const = 0;
  virtual WorldDifficulty GetWorldDifficulty() const = 0;
};

} // namespace cutum

#endif
