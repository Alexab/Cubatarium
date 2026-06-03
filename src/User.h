#ifndef USER_H
#define USER_H

#include <string>
#include <glm/glm.hpp>

namespace cutum {

using CreatureId = uint64_t;

/// Session identity: camera cache, view id, link to player creature. Inventory lives on Creature.
class User
{
public:
 User();

 const glm::vec3& GetPosition() const;
 const glm::vec3& GetViewDirection() const;
 void SetPosition(const glm::vec3& value);
 void SetViewDirection(const glm::vec3& value);

 float GetCameraYaw() const;
 float GetCameraPitch() const;
 void SetCameraOrientation(float yaw, float pitch);

 size_t GetViewId() const;
 void SetViewId(size_t value);

 CreatureId GetPlayerCreatureId() const { return playerCreatureId_; }
 void SetPlayerCreatureId(CreatureId id) { playerCreatureId_ = id; }

 const std::string& GetSelectedAppearanceTypeId() const;
 void SetSelectedAppearanceTypeId(const std::string& typeId);

private:
 glm::vec3 Position{};
 glm::vec3 ViewDirection{};
 float CameraYaw{-90.0f};
 float CameraPitch{0.0f};
 size_t ViewId{0};
 CreatureId playerCreatureId_{0};
 std::string selectedAppearanceTypeId_;
};

}

#endif
