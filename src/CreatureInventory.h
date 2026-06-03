#ifndef CREATUREINVENTORY_H
#define CREATUREINVENTORY_H

#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "InventoryTypes.h"

namespace cutum {

class CreatureInventory {
public:
 const std::map<std::string, int>& GetStorage() const { return storage_; }
 std::map<std::string, int>& GetStorageMutable() { return storage_; }
 void AddItem(const std::string& id, int count = 1);
 void AddToInventory(const std::string& id);

 size_t GetHotbarCount() const { return hotbars_.size(); }
 const HotbarBar& GetHotbar(size_t bar) const;
 const std::vector<HotbarBar>& GetHotbars() const { return hotbars_; }
 std::vector<HotbarBar>& GetHotbarsMutable() { return hotbars_; }
 size_t GetActiveBarIndex() const { return activeBarIndex_; }
 size_t GetActiveSlotIndex() const { return activeSlotIndex_; }
 void SetActiveBarIndex(size_t bar) { activeBarIndex_ = bar; }
 void SetActiveSlotIndex(size_t slot) { activeSlotIndex_ = slot; }

 const InventoryEntryRef* GetActiveEntryRef() const;
 void EnsureHotbarCount(size_t count);
 bool AssignToHotbar(size_t bar, size_t slot, const InventoryEntryRef& entry);
 void ClearHotbarSlot(size_t bar, size_t slot);
 bool SetActiveSlot(size_t bar, size_t slot);
 size_t GetActiveSlotIndex(size_t bar) const;

 const std::string& GetActiveBlockTypeName() const;
 const std::string& GetActivePrefabName() const;

 void SerializeToJson(nlohmann::json& out) const;
 void DeserializeFromJson(const nlohmann::json& data, size_t maxBarCount = 4);

private:
 std::map<std::string, int> storage_;
 std::vector<HotbarBar> hotbars_;
 size_t activeBarIndex_{0};
 size_t activeSlotIndex_{0};
};

} // namespace cutum

#endif
