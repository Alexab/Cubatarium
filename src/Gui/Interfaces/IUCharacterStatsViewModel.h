#ifndef IU_CHARACTER_STATS_VIEW_MODEL_H
#define IU_CHARACTER_STATS_VIEW_MODEL_H

#include "Creatures/Core/HumanAppearance.h"
#include "Creatures/Influence/InfluenceTypes.h"
#include "Creatures/Influence/StatusEffectTypes.h"
#include "Creatures/Stats/CreatureAttributes.h"
#include "Creatures/Stats/CreatureVitals.h"
#include "Game/Inventory/InventoryTypes.h"
#include "Game/WorldDifficulty.h"
#include "Game/WorldGameMode.h"
#include <array>
#include <string>
#include <vector>

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
  HumanAppearance humanAppearance;
  CreatureVitals vitals{};
  CreatureAttributes attributes{};
  std::array<EquippedSlotSnapshot, 4> equippedArmor{};
  std::array<EquippedSlotSnapshot, 2> equippedTools{};
  struct CarriedItem
  {
    std::string id;
    int count{0};
  };
  std::vector<CarriedItem> carriedItems;
  std::array<InventoryEntryRef, 40> slots{};
  std::vector<InventoryEntryRef> recovery;
  std::vector<StatusEffectInstance> effects;
  float movementMultiplier{1.f};
  ArmorGroups protection;
  size_t activeRow{0};
  size_t activeSlot{0};
  WorldGameMode gameMode{WorldGameMode::Creative};
  WorldDifficulty difficulty{WorldDifficulty::Normal};
  bool valid{false};
};

class IUCharacterStatsViewModel
{
public:
  virtual ~IUCharacterStatsViewModel() = default;
  virtual CharacterStatsSnapshot GetCharacterStatsSnapshot() const = 0;
  virtual bool ApplyHumanAppearance(const HumanAppearance &,
                                    const std::string &)
  {
    return false;
  }
  virtual WorldGameMode GetWorldGameMode() const = 0;
  virtual WorldDifficulty GetWorldDifficulty() const = 0;
};

} // namespace cutum

#endif
