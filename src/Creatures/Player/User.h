#ifndef USER_H
#define USER_H

#include <glm/glm.hpp>
#include <string>

namespace cutum
{

using CreatureId = uint64_t;

/// Session identity: camera cache, view Id, link to player creature. Inventory
/// lives on UCreature.
class UUser
{
public:
  UUser();

  const glm::vec3 &GetPosition() const;
  const glm::vec3 &GetViewDirection() const;
  void SetPosition(const glm::vec3 &value);
  void SetViewDirection(const glm::vec3 &value);

  float GetCameraYaw() const;
  float GetCameraPitch() const;
  void SetCameraOrientation(float yaw, float pitch);

  size_t GetViewId() const;
  void SetViewId(size_t value);

  CreatureId GetPlayerCreatureId() const { return PlayerCreatureId; }
  void SetPlayerCreatureId(CreatureId Id) { PlayerCreatureId = Id; }

  const std::string &GetSelectedAppearanceTypeId() const;
  void SetSelectedAppearanceTypeId(const std::string &typeId);

  const std::string &GetSelectedSkinId() const;
  void SetSelectedSkinId(const std::string &skinId);

private:
  glm::vec3 Position{};
  glm::vec3 ViewDirection{};
  float CameraYaw{-90.0f};
  float CameraPitch{0.0f};
  size_t ViewId{0};
  CreatureId PlayerCreatureId{0};
  std::string SelectedAppearanceTypeId;
  std::string SelectedSkinId;
};

} // namespace cutum

#endif
