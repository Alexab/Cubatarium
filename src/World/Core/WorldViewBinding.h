#ifndef WORLDVIEWBINDING_H
#define WORLDVIEWBINDING_H

#include "Creatures/Player/PlayerCapsule.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>

namespace cutum
{

class UCamera;
class UUser;
class UViewEngine;
class UWorld;

/// Bridges UWorld to Render view/camera types without pulling Render into
/// World.cpp.
class UWorldViewBinding
{
public:
  explicit UWorldViewBinding(std::shared_ptr<UViewEngine> engine);

  std::shared_ptr<UViewEngine> GetEngine() const { return Engine_; }

  std::shared_ptr<UCamera> GetUserCamera(const UWorld &world,
                                         const std::string &userName) const;
  std::shared_ptr<UCamera> GetCurrentUserCamera(UWorld &world) const;
  std::shared_ptr<UCamera> GetCurrentUserCamera(const UWorld &world) const;

  void ApplySpawnToCamera(UWorld &world);
  void ApplyUserToCamera(UWorld &world, const std::shared_ptr<UUser> &user);
  void WarmupVisibleListAtCamera(UWorld &world);
  void EnsurePlayerOnGround(UWorld &world);
  void ResetCurrentCameraVerticalPhysics(UWorld &world);

  size_t CreateUserCamera(const glm::vec3 &spawnEye);
  void SetActiveCamera(size_t viewId);

  PlayerCapsule ResolvePlacementCapsule(const UWorld &world) const;
  void RefreshIntersectionFromCurrentView(UWorld &world);
  bool TryGetCurrentViewRay(const UWorld &world, glm::vec3 &position,
                            glm::vec3 &front) const;

  static std::string ResolveUserName(const UWorld &world,
                                     const std::shared_ptr<UUser> &user);

private:
  std::shared_ptr<UViewEngine> Engine_;
};

} // namespace cutum

#endif
