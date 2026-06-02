#include "User.h"
#include "Object.h"
#include <algorithm>

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
 activePrimarySlotIndex_ = 0;
 activeSecondarySlotIndex_ = 0;
 activeBarIndex_ = 0;
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

void User::SetActiveBlockIndex(size_t index)
{
 if (index >= kHotbarSlots || hotbars_.empty()) {
  return;
 }
 activePrimarySlotIndex_ = index;
 activeBarIndex_ = 0;
 if (!hotbars_[0].slots[index].empty
     && hotbars_[0].slots[index].entry.kind == InventoryEntryKind::Object
     && hotbars_.size() > 1) {
  activeSecondarySlotIndex_ = index;
 }
}

void User::SetActivePrefabIndex(size_t index)
{
 if (index >= kHotbarSlots || hotbars_.empty()) {
  return;
 }
 activeBarIndex_ = hotbars_.size() > 1 ? 1 : 0;
 if (hotbars_.size() > 1) {
  activeSecondarySlotIndex_ = index;
 }
 if (hotbars_[activeBarIndex_].slots[index].empty) {
  return;
 }
}

void User::SetActiveObjectTypeName(const std::string& block_type)
{
 for (size_t b = 0; b < hotbars_.size(); ++b) {
  for (size_t i = 0; i < kHotbarSlots; ++i) {
   const HotbarSlot& slot = hotbars_[b].slots[i];
   if (!slot.empty && slot.entry.id == block_type) {
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
 hotbars_[bar].slots[slot].empty = entry.empty;
 hotbars_[bar].slots[slot].entry = entry;
 hotbars_[bar].slots[slot].entry.empty = entry.empty;
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
 activeBarIndex_ = bar;
 if (bar == 0) {
  activePrimarySlotIndex_ = slot;
 } else {
  activeSecondarySlotIndex_ = slot;
 }
 return true;
}

size_t User::GetActiveSlotIndex(size_t bar) const
{
 if (bar == 0) {
  return activePrimarySlotIndex_;
 }
 return activeSecondarySlotIndex_;
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
 if (s.empty || s.entry.empty) {
  return nullptr;
 }
 return &s.entry;
}

void User::ClampActiveIndices()
{
 if (hotbars_.empty()) {
  hotbars_.resize(1);
 }
 if (activePrimarySlotIndex_ >= kHotbarSlots) {
  activePrimarySlotIndex_ = 0;
 }
 if (activeSecondarySlotIndex_ >= kHotbarSlots) {
  activeSecondarySlotIndex_ = 0;
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

}
