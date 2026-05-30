#ifndef USER_H
#define USER_H

#include <map>
#include <string>
#include <memory>
#include <vector>
#include <glm/glm.hpp>

namespace cutum {

class Object;

enum class HotbarItemKind {
 Block,
 Prefab
};

struct HotbarSlot {
 HotbarItemKind kind{HotbarItemKind::Block};
 std::string id;
};

class User
{
public:
 User();

 const std::map<std::string, int>& GetInventory() const;
 void AddToInventory(const std::string &cube_type);

 const std::string& GetActiveObjectTypeName() const;
 void SetActiveObjectTypeName(const std::string& cube_type);
 void SetActiveObjectTypeNameByIndex(size_t index);

 const HotbarSlot& GetActiveHotbarSlot() const;
 const std::vector<HotbarSlot>& GetHotbar() const;

 std::shared_ptr<Object> GetActiveObject();

 const glm::vec3& GetPosition() const;
 const glm::vec3& GetViewDirection() const;

 void SetPosition(const glm::vec3& value);
 void SetViewDirection(const glm::vec3& value);

 float GetCameraYaw() const;
 float GetCameraPitch() const;
 void SetCameraOrientation(float yaw, float pitch);

 size_t GetViewId() const;
 void SetViewId(size_t value);

private:
 void InitDefaultHotbar();

 std::map<std::string, int> Inventory;
 std::vector<HotbarSlot> hotbar_;
 size_t activeHotbarIndex_{0};

 std::shared_ptr<Object> ActiveObject;
 std::string ActiveObjectTypeName;
 glm::vec3 Position;
 glm::vec3 ViewDirection;
 float CameraYaw{-90.0f};
 float CameraPitch{0.0f};
 size_t ViewId;
};

}

#endif // USER_H
