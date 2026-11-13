#ifndef CREATUREINVENTORY_H
#define CREATUREINVENTORY_H
#include "Creatures/Influence/InfluenceTypes.h"
#include "Game/Inventory/InventoryTypes.h"
#include <algorithm>
#include <array>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
namespace cutum
{
class UItemDefinitionStorage;
/// Four physical rows. Hotbars are views of these rows, never stock mirrors.
class UCreatureInventory
{
public:
  static constexpr size_t kRowCount = 4;
  static constexpr size_t kSlotsPerRow = 10;
  static constexpr size_t kSlotCount = kRowCount * kSlotsPerRow;
  static constexpr size_t kArmorSlotCount = 4;
  void SetItemDefinitions(const UItemDefinitionStorage *items);
  const std::map<std::string, int> &GetStorage() const;
  InventoryOperationResult Insert(InventoryEntryRef entry);
  InventoryOperationResult Consume(InventoryEntryKind kind,
                                   const std::string &id, int count = 1);
  InventoryOperationResult Move(size_t from, size_t to, int count = -1);
  InventoryOperationResult Take(size_t slot, int count, InventoryEntryRef &out);
  bool AssignOwned(size_t bar, size_t slot, const InventoryEntryRef &entry);
  int Count(InventoryEntryKind kind, const std::string &id) const;
  int StackLimit(const InventoryEntryRef &entry) const;
  InventoryEntryKind KindForId(const std::string &id) const;
  const InventoryEntryRef &GetSlot(size_t slot) const;
  InventoryEntryRef *GetSlotMutable(size_t slot);
  void Sort();
  InventoryOperationResult QuickMove(size_t slot);
  void CollectMatching(size_t slot);
  const std::vector<InventoryEntryRef> &GetRecovery() const { return Recovery; }
  InventoryOperationResult Recover(size_t index);
  const std::vector<std::string> &GetMigrationWarnings() const
  {
    return MigrationWarnings;
  }
  bool NeedsMigrationBackup() const { return LegacyMigration; }
  bool IsCreative() const { return Creative; }
  void SetCreative(bool creative);
  void AddItem(const std::string &id, int count = 1);
  void AddToInventory(const std::string &id) { AddItem(id); }
  void InitCreativeDefaults();
  void MigrateCreativeStorageToSurvival() { SetCreative(false); }
  void EnsureDefaultHotbar();
  bool IsPrimaryHotbarEmpty() const;
  void SetObjectHotbar(const std::vector<std::string> &names);
  size_t GetHotbarCount() const { return kRowCount; }
  const HotbarBar &GetHotbar(size_t bar) const;
  const std::vector<HotbarBar> &GetHotbars() const
  {
    return Creative ? CreativeHotbars : Hotbars;
  }
  const std::vector<HotbarBar> &GetOwnedHotbars() const { return Hotbars; }
  std::vector<HotbarBar> &GetHotbarsMutable()
  {
    return Creative ? CreativeHotbars : Hotbars;
  }
  size_t GetActiveBarIndex() const { return ActiveBarIndex; }
  size_t GetActiveSlotIndex() const { return ActiveSlotIndex; }
  void SetActiveBarIndex(size_t bar)
  {
    ActiveBarIndex = std::min(bar, kRowCount - 1);
  }
  void SetActiveSlotIndex(size_t slot)
  {
    ActiveSlotIndex = std::min(slot, kSlotsPerRow - 1);
  }
  const InventoryEntryRef *GetActiveEntryRef() const;
  InventoryEntryRef *GetActiveEntryRef();
  void EnsureHotbarCount(size_t) {} // HUD settings never change capacity
  bool AssignToHotbar(size_t bar, size_t slot, const InventoryEntryRef &entry);
  void ClearHotbarSlot(size_t bar, size_t slot);
  bool SetActiveSlot(size_t bar, size_t slot);
  size_t GetActiveSlotIndex(size_t bar) const;
  const std::string &GetActiveBlockTypeName() const;
  const std::string &GetActiveObjectName() const;
  void SerializeToJson(nlohmann::json &out) const;
  void DeserializeFromJson(const nlohmann::json &data, size_t maxBarCount = 4);
  const InventoryEntryRef &GetEquippedArmor(size_t slot) const;
  bool EquipArmor(size_t slot, const InventoryEntryRef &entry,
                  const UItemDefinitionStorage &items,
                  bool consumeFromStorage = true);
  void UnequipArmor(size_t slot, const UItemDefinitionStorage &items);
  const ArmorGroups &GetEquippedArmorGroups() const
  {
    return EquippedArmorGroups;
  }
  const InventoryEntryRef &GetEquippedOffhand() const;
  bool EquipOffhand(const InventoryEntryRef &entry);
  bool EquipOffhand(const InventoryEntryRef &entry,
                    const UItemDefinitionStorage &items,
                    bool consumeFromStorage = true);
  void UnequipOffhand();
  void UnequipOffhand(const UItemDefinitionStorage &items);
  const ArmorGroups &GetOffhandArmorGroups() const
  {
    return OffhandArmorGroups;
  }
  void RefreshEquipment(const UItemDefinitionStorage &items);
  void WearArmor(size_t slot, float delta, const UItemDefinitionStorage &items);
  void WearOffhand(float delta, const UItemDefinitionStorage &items);
  bool RepairActive(const UItemDefinitionStorage &items, std::string &reason);

private:
  static bool CanMerge(const InventoryEntryRef &a, const InventoryEntryRef &b);
  InventoryEntryRef Prepare(InventoryEntryRef entry);
  bool Equip(InventoryEntryRef &target, const InventoryEntryRef &entry,
             bool consume);
  bool ReturnEquipment(InventoryEntryRef &entry);
  std::array<InventoryEntryRef, kArmorSlotCount> &Armor()
  {
    return Creative ? CreativeArmor : EquippedArmor;
  }
  const std::array<InventoryEntryRef, kArmorSlotCount> &Armor() const
  {
    return Creative ? CreativeArmor : EquippedArmor;
  }
  InventoryEntryRef &Offhand()
  {
    return Creative ? CreativeOffhand : EquippedOffhand;
  }
  const InventoryEntryRef &Offhand() const
  {
    return Creative ? CreativeOffhand : EquippedOffhand;
  }
  std::vector<HotbarBar> Hotbars{kRowCount};
  std::vector<HotbarBar> CreativeHotbars{kRowCount};
  std::array<InventoryEntryRef, kArmorSlotCount> EquippedArmor{},
      CreativeArmor{};
  InventoryEntryRef EquippedOffhand{}, CreativeOffhand{};
  ArmorGroups EquippedArmorGroups{}, OffhandArmorGroups{};
  mutable std::map<std::string, int> StorageView;
  std::vector<InventoryEntryRef> Recovery;
  std::vector<std::string> MigrationWarnings;
  const UItemDefinitionStorage *Items{nullptr};
  std::uint64_t NextInstanceId{1};
  size_t ActiveBarIndex{0}, ActiveSlotIndex{0};
  bool Creative{false}, LegacyMigration{false};
};
} // namespace cutum
#endif
