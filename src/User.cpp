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
}

void User::SetActiveObjectTypeNameByIndex(size_t index)
{
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

std::shared_ptr<Object> User::GetActiveObject()
{
 return ActiveObject;
}

const QVector3D& User::GetPosition() const
{
 return Position;
}

const QVector3D& User::GetViewDirection() const
{
 return ViewDirection;
}

void User::SetPosition(const QVector3D& value)
{
 Position = value;
}

void User::SetViewDirection(const QVector3D& value)
{
 ViewDirection = value;
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
