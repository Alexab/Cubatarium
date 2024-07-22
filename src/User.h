#ifndef USER_H
#define USER_H

#include <map>
#include <string>
#include <memory>
#include <unordered_map>
#include <QVector3D>

namespace cutum {

class Object;

class User
{
public:
 User();

 const std::map<std::string, int>& GetInventory() const;
 void AddToInventory(const std::string &cube_type);

 const std::string& GetActiveObjectTypeName() const;
 void SetActiveObjectTypeName(const std::string& cube_type);
 void SetActiveObjectTypeNameByIndex(size_t index);

 std::shared_ptr<Object> GetActiveObject();

 const QVector3D& GetPosition() const;
 const QVector3D& GetViewDirection() const;

 void SetPosition(const QVector3D& value);
 void SetViewDirection(const QVector3D& value);

 size_t GetViewId() const;
 void SetViewId(size_t value);

private:
 std::map<std::string, int> Inventory;

 std::shared_ptr<Object> ActiveObject;

 std::string ActiveObjectTypeName;

 QVector3D Position;

 QVector3D ViewDirection;

 size_t ViewId;
};

}

#endif // USER_H
