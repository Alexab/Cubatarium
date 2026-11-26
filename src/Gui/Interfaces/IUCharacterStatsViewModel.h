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
#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace cutum
{

struct CharacterLookSnapshot
{
  struct EquippedSlotSnapshot
  {
    std::string itemId;
    float wear{0.f};
    bool broken{false};
    bool isBlock{false};
  };

  uint64_t OwnerId{0};
  std::string typeId;
  std::string skinId;
  HumanAppearance humanAppearance;
  std::array<EquippedSlotSnapshot, 4> equippedArmor{};
  std::array<EquippedSlotSnapshot, 2> equippedTools{};
  bool valid{false};
};

struct CharacterPageSnapshot : CharacterLookSnapshot
{
  std::string displayName;
  CreatureVitals vitals{};
  CreatureAttributes attributes{};
  std::array<InventoryEntryRef, 40> slots{};
  // Local rows of the requested recovery page. Full snapshots own every row.
  std::vector<InventoryEntryRef> recovery;
  size_t recoveryCount{0};
  std::vector<StatusEffectInstance> effects;
  float movementMultiplier{1.f};
  int fleshyProtection{0};
  size_t activeRow{0};
  size_t activeSlot{0};
  WorldGameMode gameMode{WorldGameMode::Creative};
  WorldDifficulty difficulty{WorldDifficulty::Normal};
};

struct CharacterStatsSnapshot : CharacterPageSnapshot
{
  struct CarriedItem
  {
    std::string id;
    int count{0};
  };
  std::vector<CarriedItem> carriedItems;
  ArmorGroups protection;
};

class IUCharacterStatsViewModel
{
public:
  virtual ~IUCharacterStatsViewModel() = default;
  virtual CharacterStatsSnapshot GetCharacterStatsSnapshot() const = 0;
  virtual CharacterLookSnapshot GetCharacterLookSnapshot() const
  {
    return GetCharacterStatsSnapshot();
  }
  virtual CharacterPageSnapshot GetCharacterPageSnapshot(size_t start, size_t count) const
  {
    auto full = GetCharacterStatsSnapshot();
    full.recoveryCount = full.recovery.size();
    full.fleshyProtection = full.protection.Get("fleshy");
    auto recovery = std::move(full.recovery);
    CharacterPageSnapshot page = std::move(full);
    start = std::min(start, recovery.size());
    count = std::min({count, page.slots.size(), recovery.size() - start});
    if (count)
      page.recovery.assign(std::make_move_iterator(recovery.begin() + start),
                           std::make_move_iterator(recovery.begin() + start + count));
    return page;
  }
  virtual size_t GetCharacterRecoveryCount() const
  {
    return GetCharacterStatsSnapshot().recovery.size();
  }
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
