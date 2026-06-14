#include "User.h"

namespace cutum {

UUser::UUser() = default;

const glm::vec3& UUser::GetPosition() const
{
 return Position;
}

const glm::vec3& UUser::GetViewDirection() const
{
 return ViewDirection;
}

void UUser::SetPosition(const glm::vec3& value)
{
 Position = value;
}

void UUser::SetViewDirection(const glm::vec3& value)
{
 ViewDirection = value;
}

float UUser::GetCameraYaw() const
{
 return CameraYaw;
}

float UUser::GetCameraPitch() const
{
 return CameraPitch;
}

void UUser::SetCameraOrientation(float yaw, float pitch)
{
 CameraYaw = yaw;
 CameraPitch = pitch;
}

size_t UUser::GetViewId() const
{
 return ViewId;
}

void UUser::SetViewId(size_t value)
{
 ViewId = value;
}

const std::string& UUser::GetSelectedAppearanceTypeId() const
{
 return SelectedAppearanceTypeId;
}

void UUser::SetSelectedAppearanceTypeId(const std::string& typeId)
{
 SelectedAppearanceTypeId = typeId;
}

const std::string& UUser::GetSelectedSkinId() const
{
 return SelectedSkinId;
}

void UUser::SetSelectedSkinId(const std::string& skinId)
{
 SelectedSkinId = skinId;
}

}
