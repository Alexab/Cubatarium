#ifndef USER_H
#define USER_H

#include <map>
#include <string>
#include <memory>
#include <vector>
#include <glm/glm.hpp>

namespace cutum {

class Object;

class User
{
public:
 User();

 const std::map<std::string, int>& GetInventory() const;
 void AddToInventory(const std::string &cube_type);

 const std::string& GetActiveBlockTypeName() const;
 const std::string& GetActiveObjectTypeName() const;
 const std::string& GetActivePrefabName() const;

 void SetActiveBlockIndex(size_t index);
 void SetActivePrefabIndex(size_t index);
 void SetActiveObjectTypeName(const std::string& block_type);
 void SetPrefabHotbar(const std::vector<std::string>& prefab_names);

 const std::vector<std::string>& GetBlockHotbar() const;
 const std::vector<std::string>& GetPrefabHotbar() const;

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
 std::vector<std::string> blockHotbar_;
 std::vector<std::string> prefabHotbar_;
 size_t activeBlockIndex_{0};
 size_t activePrefabIndex_{0};

 std::shared_ptr<Object> ActiveObject;
 glm::vec3 Position;
 glm::vec3 ViewDirection;
 float CameraYaw{-90.0f};
 float CameraPitch{0.0f};
 size_t ViewId;
};

}

#endif // USER_H
