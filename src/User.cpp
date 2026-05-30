#include "User.h"
#include "Object.h"

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
 hotbar_ = {
     {HotbarItemKind::Block, "wood"},
     {HotbarItemKind::Block, "grass"},
     {HotbarItemKind::Block, "stone"},
     {HotbarItemKind::Block, "tree_birch"},
     {HotbarItemKind::Block, "pumpkin"},
     {HotbarItemKind::Block, "sandstone"},
     {HotbarItemKind::Block, "stonebrick"},
     {HotbarItemKind::Block, "tnt"},
     {HotbarItemKind::Block, "brick"},
     {HotbarItemKind::Prefab, "tree_small"},
 };
 activeHotbarIndex_ = 1;
 ActiveObjectTypeName = hotbar_[activeHotbarIndex_].id;
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

const std::string& User::GetActiveObjectTypeName() const
{
 return ActiveObjectTypeName;
}

void User::SetActiveObjectTypeName(const std::string& object_type)
{
 ActiveObjectTypeName = object_type;
 for (size_t i = 0; i < hotbar_.size(); ++i) {
  if (hotbar_[i].id == object_type) {
   activeHotbarIndex_ = i;
   break;
  }
 }
}

void User::SetActiveObjectTypeNameByIndex(size_t index)
{
 if (index < hotbar_.size()) {
  activeHotbarIndex_ = index;
  ActiveObjectTypeName = hotbar_[index].id;
  return;
 }

 size_t i = 0;
 for(auto I = Inventory.begin(); I != Inventory.end(); ++I)
 {
  if(i == index)
  {
   SetActiveObjectTypeName(I->first);
   break;
  }
  ++i;
 }
}

const HotbarSlot& User::GetActiveHotbarSlot() const
{
 return hotbar_[activeHotbarIndex_];
}

const std::vector<HotbarSlot>& User::GetHotbar() const
{
 return hotbar_;
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
