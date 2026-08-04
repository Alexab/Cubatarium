#ifndef CREATUREINVENTORY_H
#define CREATUREINVENTORY_H

#include "Game/Inventory/InventoryTypes.h"
#include "Creatures/Influence/InfluenceTypes.h"
#include <map>
#include <array>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace cutum
{

class UItemDefinitionStorage;

class UCreatureInventory
{
public:
  const std::map<std::string, int> &GetStorage() const { return Storage; }
  std::map<std::string, int> &GetStorageMutable() { return Storage; }
  void AddItem(const std::string &Id, int count = 1);
  void AddToInventory(const std::string &Id);

  /// Creative-mode defaults (counts -1 = unlimited).
  void InitCreativeDefaults();
  void EnsureDefaultHotbar();
  bool IsPrimaryHotbarEmpty() const;
  void SetObjectHotbar(const std::vector<std::string> &object_names);

  size_t GetHotbarCount() const { return Hotbars.size(); }
  const HotbarBar &GetHotbar(size_t bar) const;
  const std::vector<HotbarBar> &GetHotbars() const { return Hotbars; }
  std::vector<HotbarBar> &GetHotbarsMutable() { return Hotbars; }
  size_t GetActiveBarIndex() const { return ActiveBarIndex; }
  size_t GetActiveSlotIndex() const { return ActiveSlotIndex; }
  void SetActiveBarIndex(size_t bar) { ActiveBarIndex = bar; }
  void SetActiveSlotIndex(size_t slot) { ActiveSlotIndex = slot; }

  const InventoryEntryRef *GetActiveEntryRef() const;
  InventoryEntryRef *GetActiveEntryRef();
  void EnsureHotbarCount(size_t count);
  bool AssignToHotbar(size_t bar, size_t slot, const InventoryEntryRef &entry);
  void ClearHotbarSlot(size_t bar, size_t slot);
  bool SetActiveSlot(size_t bar, size_t slot);
  size_t GetActiveSlotIndex(size_t bar) const;

  const std::string &GetActiveBlockTypeName() const;
  const std::string &GetActiveObjectName() const;

  void SerializeToJson(nlohmann::json &out) const;
  void DeserializeFromJson(const nlohmann::json &data, size_t maxBarCount = 4);

  // Armor equipment (0..5):
  // 0=head,1=chest,2=arms,3=hands,4=legs,5=feet.
  const InventoryEntryRef &GetEquippedArmor(size_t slot) const;
  bool EquipArmor(size_t slot, const InventoryEntryRef &entry,
                   const UItemDefinitionStorage &items);
  void UnequipArmor(size_t slot, const UItemDefinitionStorage &items);
  const ArmorGroups &GetEquippedArmorGroups() const { return EquippedArmorGroups; }

  const InventoryEntryRef &GetEquippedOffhand() const;
  bool EquipOffhand(const InventoryEntryRef &entry);
  void UnequipOffhand();

private:
  std::map<std::string, int> Storage;
  std::vector<HotbarBar> Hotbars;
  size_t ActiveBarIndex{0};
  size_t ActiveSlotIndex{0};

  std::array<InventoryEntryRef, 6> EquippedArmor{};
  ArmorGroups EquippedArmorGroups{};
  InventoryEntryRef EquippedOffhand{};
};

} // namespace cutum

#endif
