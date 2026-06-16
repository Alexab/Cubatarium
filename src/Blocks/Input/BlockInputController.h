#ifndef BLOCKINPUTCONTROLLER_H
#define BLOCKINPUTCONTROLLER_H

#include "App/Platform/InputManager.h"
#include "App/Settings/UiSettings.h"
#include "Game/Inventory/InventoryTypes.h"
#include <chrono>
#include <glm/glm.hpp>
#include <memory>

struct GLFWwindow;

namespace cutum
{

class UApplication;
class UGeometryEngine;
class UWorld;

struct BlockInputContext
{
  std::shared_ptr<UWorld> World;
  UGeometryEngine *Geometries{nullptr};
  const UiSettings *Ui{nullptr};
  GLFWwindow *Window{nullptr};
  UApplication *App{nullptr};
};

class UBlockInputController
{
public:
  void OnMouseButton(MouseButton Button, bool Pressed, glm::vec2 pos,
                     const BlockInputContext &ctx);
  void OnMouseMove(glm::vec2 pos, glm::vec2 delta,
                   const BlockInputContext &ctx);
  void OnKeyDelete(const BlockInputContext &ctx);
  void Tick(float dt, const BlockInputContext &ctx);

  void OnQuickTap(const BlockInputContext &ctx);
  void CancelPointerInteraction(const BlockInputContext &ctx);

  bool IsRightLookActive() const { return RightLookActive; }

private:
  const InventoryEntryRef *GetActiveEntry(const BlockInputContext &ctx) const;
  bool ActiveSlotBlocksWorldInteraction(const BlockInputContext &ctx) const;

  void HandleLeftPress(const BlockInputContext &ctx);
  void HandleLeftRelease(float holdSeconds, const BlockInputContext &ctx);
  void HandleRightPress(glm::vec2 pos, const BlockInputContext &ctx);
  void HandleRightRelease(const BlockInputContext &ctx);

  void TryPlaceFromActiveSlot(const BlockInputContext &ctx);
  void TrySpawnCreatureOrSkin(const BlockInputContext &ctx);
  void TryInstantBreak(const BlockInputContext &ctx);

  std::chrono::steady_clock::time_point LeftDownTime{};
  bool LeftHeld{false};
  glm::vec2 RightDownPos{0.0f};
  bool RightPressed{false};
  bool RightLookActive{false};
  bool RightDragExceeded{false};
};

} // namespace cutum

#endif
