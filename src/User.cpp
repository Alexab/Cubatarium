#include "User.h"

namespace cutum {

User::User() = default;

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

const std::string& User::GetSelectedAppearanceTypeId() const
{
 return selectedAppearanceTypeId_;
}

void User::SetSelectedAppearanceTypeId(const std::string& typeId)
{
 selectedAppearanceTypeId_ = typeId;
}

}
