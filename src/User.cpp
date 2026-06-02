#include "User.h"
#include "Object.h"
#include <algorithm>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum {

namespace {

constexpr size_t kHotbarSlots = 10;
constexpr size_t kMaxHotbars = 2;

}

User::User()
{
 Inventory["wood"] = -1;
 Inventory["grass"] = -1;
 Inventory["stone"] = -1;
 Inventory["tree_birch"] = -1;
 Inventory["pumpkin"] = -1;
 Inventory["sandstone"] = -1;
 Inventory["stonebrick"] = -1;
 Inventory["tnt"] = -1;
 Inventory["brick"] = -1;
 Inventory["bedrock"] = -1;
 Inventory["water"] = -1;
 Inventory["lava"] = -1;
 Inventory["fire"] = -1;

 ViewId = 0;
 InitDefaultHotbar();
}

void User::InitDefaultHotbar()
{
 hotbars_.clear();
 hotbars_.resize(1);
 activeBarIndex_ = 0;
 activeSlotIndex_ = 0;
}

void User::SetPrefabHotbar(const std::vector<std::string>& prefab_names)
{
 EnsureHotbarCount(1);
 size_t idx = 0;
 for (const std::string& name : prefab_names) {
  if (idx >= kHotbarSlots) {
   break;
  }
  hotbars_[0].slots[idx].empty = false;
  hotbars_[0].slots[idx].entry.kind = InventoryEntryKind::Object;
  hotbars_[0].slots[idx].entry.id = name;
  hotbars_[0].slots[idx].entry.count = 0;
  hotbars_[0].slots[idx].entry.empty = false;
  ++idx;
 }
}

const std::map<std::string, int>& User::GetInventory() const
{
 return Inventory;
}

void User::AddToInventory(const std::string &object_type)
{
 if(Inventory[object_type] <0)
  return;

 Inventory[object_type]++;
}

const std::string& User::GetActiveBlockTypeName() const
{
 const InventoryEntryRef* active = GetActiveEntryRef();
 if (active && !active->empty && active->kind == InventoryEntryKind::Block) {
  return active->id;
 }
 return EmptyString();
}

const std::string& User::GetActiveObjectTypeName() const
{
 return GetActiveBlockTypeName();
}

const std::string& User::GetActivePrefabName() const
{
 const InventoryEntryRef* active = GetActiveEntryRef();
 if (active && !active->empty && active->kind == InventoryEntryKind::Object) {
  return active->id;
 }
 return EmptyString();
}

const InventoryEntryRef* User::GetActiveHotbarEntry() const
{
 return GetActiveEntryRef();
}

void User::SetActiveBlockIndex(size_t index)
{
 if (index >= kHotbarSlots || hotbars_.empty()) {
  return;
 }
 SetActiveSlot(0, index);
}

void User::SetActivePrefabIndex(size_t index)
{
 if (index >= kHotbarSlots || hotbars_.empty()) {
  return;
 }
 SetActiveSlot(hotbars_.size() > 1 ? 1 : 0, index);
}

void User::SetActiveObjectTypeName(const std::string& block_type)
{
 for (size_t b = 0; b < hotbars_.size(); ++b) {
  for (size_t i = 0; i < kHotbarSlots; ++i) {
   const HotbarSlot& slot = hotbars_[b].slots[i];
   if (!slot.entry.id.empty() && slot.entry.id == block_type) {
    SetActiveSlot(b, i);
    return;
   }
  }
 }
}

std::vector<std::string> User::GetBlockHotbar() const
{
 std::vector<std::string> result(kHotbarSlots);
 if (hotbars_.empty()) {
  return result;
 }
 for (size_t i = 0; i < kHotbarSlots; ++i) {
  const HotbarSlot& slot = hotbars_[0].slots[i];
  if (!slot.empty && slot.entry.kind == InventoryEntryKind::Block) {
   result[i] = slot.entry.id;
  }
 }
 return result;
}

std::vector<std::string> User::GetPrefabHotbar() const
{
 std::vector<std::string> result(kHotbarSlots);
 const size_t bar = hotbars_.size() > 1 ? 1 : 0;
 if (hotbars_.empty()) {
  return result;
 }
 for (size_t i = 0; i < kHotbarSlots; ++i) {
  const HotbarSlot& slot = hotbars_[bar].slots[i];
  if (!slot.empty && slot.entry.kind == InventoryEntryKind::Object) {
   result[i] = slot.entry.id;
  }
 }
 return result;
}

void User::EnsureHotbarCount(size_t count)
{
 const size_t clamped = std::max<size_t>(1, std::min(count, kMaxHotbars));
 hotbars_.resize(clamped);
 ClampActiveIndices();
}

size_t User::GetHotbarCount() const
{
 return hotbars_.size();
}

const HotbarBar& User::GetHotbar(size_t bar) const
{
 static HotbarBar kEmptyBar;
 if (bar >= hotbars_.size()) {
  return kEmptyBar;
 }
 return hotbars_[bar];
}

bool User::AssignToHotbar(size_t bar, size_t slot, const InventoryEntryRef& entry)
{
 if (bar >= hotbars_.size() || slot >= kHotbarSlots) {
  return false;
 }
 HotbarSlot& slotRef = hotbars_[bar].slots[slot];
 slotRef.entry = entry;
 const bool hasItem = !entry.id.empty();
 slotRef.entry.empty = !hasItem;
 slotRef.empty = !hasItem;
 return true;
}

void User::ClearHotbarSlot(size_t bar, size_t slot)
{
 if (bar >= hotbars_.size() || slot >= kHotbarSlots) {
  return;
 }
 hotbars_[bar].slots[slot] = HotbarSlot{};
}

bool User::SetActiveSlot(size_t bar, size_t slot)
{
 if (bar >= hotbars_.size() || slot >= kHotbarSlots) {
  return false;
 }
 std::cerr << "[hotbar] set active bar=" << bar << " slot=" << slot << std::endl;
 activeBarIndex_ = bar;
 activeSlotIndex_ = slot;
 return true;
}

size_t User::GetActiveSlotIndex(size_t bar) const
{
 if (bar == activeBarIndex_) {
  return activeSlotIndex_;
 }
 return kHotbarSlots;
}

const std::string& User::EmptyString()
{
 static const std::string kEmpty;
 return kEmpty;
}

const InventoryEntryRef* User::GetActiveEntryRef() const
{
 if (hotbars_.empty()) {
  return nullptr;
 }
 const size_t bar = std::min(activeBarIndex_, hotbars_.size() - 1);
 const size_t slot = GetActiveSlotIndex(bar);
 if (slot >= kHotbarSlots) {
  return nullptr;
 }
 const HotbarSlot& s = hotbars_[bar].slots[slot];
 if (s.entry.id.empty()) {
  return nullptr;
 }
 return &s.entry;
}

void User::ClampActiveIndices()
{
 if (hotbars_.empty()) {
  hotbars_.resize(1);
 }
 if (activeSlotIndex_ >= kHotbarSlots) {
  activeSlotIndex_ = 0;
 }
 if (activeBarIndex_ >= hotbars_.size()) {
  activeBarIndex_ = 0;
 }
}

std::shared_ptr<Object> User::GetActiveObject()
{
 return ActiveObject;
}

const glm::vec3& User::GetPosition() const
{
 return Position;
}

const glm::vec3& User::GetViewDirection() const
{
 return ViewDirection;
}

void User::SetPosition(const glm::vec3& value)
{
 Position = value;
}

void User::SetViewDirection(const glm::vec3& value)
{
 ViewDirection = value;
}

float User::GetCameraYaw() const
{
 return CameraYaw;
}

float User::GetCameraPitch() const
{
 return CameraPitch;
}

void User::SetCameraOrientation(float yaw, float pitch)
{
 CameraYaw = yaw;
 CameraPitch = pitch;
}

size_t User::GetViewId() const
{
 return ViewId;
}

void User::SetViewId(size_t value)
{
 ViewId = value;
}

nlohmann::json User::SerializeHotbars() const
{
 using json = nlohmann::json;
 json bars = json::array();
 for (const HotbarBar& bar : hotbars_) {
  json slots = json::array();
  for (const HotbarSlot& slot : bar.slots) {
   if (slot.empty || slot.entry.empty) {
    slots.push_back(json{{"empty", true}});
   } else {
    slots.push_back(json{{"empty", false},
                         {"kind", slot.entry.kind == InventoryEntryKind::Block ? "block" : "object"},
                         {"id", slot.entry.id}});
   }
  }
  bars.push_back(json{{"slots", slots}});
 }
 json result;
 result["hotbars"] = bars;
 result["active_bar"] = activeBarIndex_;
 result["active_slot"] = activeSlotIndex_;
 return result;
}

void User::DeserializeHotbars(const nlohmann::json& userData, size_t maxBarCount)
{
 using json = nlohmann::json;
 if (!userData.contains("hotbars") || !userData["hotbars"].is_array()) {
  return;
 }

 const json& barsJson = userData["hotbars"];
 hotbars_.clear();
 hotbars_.resize(barsJson.size());

 for (size_t b = 0; b < barsJson.size() && b < hotbars_.size(); ++b) {
  const json& barJson = barsJson[b];
  if (!barJson.contains("slots") || !barJson["slots"].is_array()) {
   continue;
  }
  const json& slotsJson = barJson["slots"];
  for (size_t s = 0; s < kHotbarSlots && s < slotsJson.size(); ++s) {
   const json& slotJson = slotsJson[s];
   HotbarSlot& slot = hotbars_[b].slots[s];
   slot = HotbarSlot{};
   if (slotJson.value("empty", true)) {
    continue;
   }
   const std::string kind = slotJson.value("kind", "block");
   slot.entry.kind = (kind == "object") ? InventoryEntryKind::Object : InventoryEntryKind::Block;
   slot.entry.id = slotJson.value("id", "");
   slot.entry.empty = slot.entry.id.empty();
   slot.empty = slot.entry.empty;
   if (!slot.empty) {
    slot.entry.count = 0;
   }
  }
 }

 if (userData.contains("active_bar")) {
  activeBarIndex_ = userData["active_bar"].get<size_t>();
 }
 if (userData.contains("active_slot")) {
  activeSlotIndex_ = userData["active_slot"].get<size_t>();
 }

 const size_t clampedMax = std::max<size_t>(1, std::min(maxBarCount, kMaxHotbars));
 if (hotbars_.size() > clampedMax) {
  hotbars_.resize(clampedMax);
 }
 if (hotbars_.empty()) {
  hotbars_.resize(1);
 }
 ClampActiveIndices();
}

}
