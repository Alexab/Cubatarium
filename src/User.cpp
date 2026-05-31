#include "User.h"
#include "Object.h"
#include <algorithm>

namespace cutum {

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

 ViewId = 0;
 InitDefaultHotbar();
}

void User::InitDefaultHotbar()
{
 blockHotbar_ = {
     "wood",
     "grass",
     "dirt",
     "stone",
     "tree_birch",
     "pumpkin",
     "sandstone",
     "stonebrick",
     "tnt",
     "brick",
     "bedrock",
 };
 prefabHotbar_ = {"tree_small"};
 activeBlockIndex_ = 1;
 activePrefabIndex_ = 0;
}

void User::SetPrefabHotbar(const std::vector<std::string>& prefab_names)
{
 prefabHotbar_.clear();
 constexpr size_t kMaxPrefabSlots = 10;
 for (const std::string& name : prefab_names) {
  if (prefabHotbar_.size() >= kMaxPrefabSlots) {
   break;
  }
  prefabHotbar_.push_back(name);
 }
 if (prefabHotbar_.empty()) {
  prefabHotbar_.push_back("tree_small");
 }
 if (activePrefabIndex_ >= prefabHotbar_.size()) {
  activePrefabIndex_ = 0;
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
 return blockHotbar_[activeBlockIndex_];
}

const std::string& User::GetActiveObjectTypeName() const
{
 return GetActiveBlockTypeName();
}

const std::string& User::GetActivePrefabName() const
{
 if (prefabHotbar_.empty()) {
  static const std::string kEmpty;
  return kEmpty;
 }
 return prefabHotbar_[activePrefabIndex_];
}

void User::SetActiveBlockIndex(size_t index)
{
 if (index < blockHotbar_.size()) {
  activeBlockIndex_ = index;
 }
}

void User::SetActivePrefabIndex(size_t index)
{
 if (index < prefabHotbar_.size()) {
  activePrefabIndex_ = index;
 }
}

void User::SetActiveObjectTypeName(const std::string& block_type)
{
 for (size_t i = 0; i < blockHotbar_.size(); ++i) {
  if (blockHotbar_[i] == block_type) {
   activeBlockIndex_ = i;
   break;
  }
 }
}

const std::vector<std::string>& User::GetBlockHotbar() const
{
 return blockHotbar_;
}

const std::vector<std::string>& User::GetPrefabHotbar() const
{
 return prefabHotbar_;
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
