#ifndef BLOCKINPUTCONTROLLER_H
#define BLOCKINPUTCONTROLLER_H

#include "InputManager.h"
#include "InventoryTypes.h"
#include "UiSettings.h"
#include <chrono>
#include <glm/glm.hpp>
#include <memory>

struct GLFWwindow;

namespace cutum {

class Application;
class GeometryEngine;
class World;

struct BlockInputContext {
    std::shared_ptr<World> world;
    GeometryEngine* geometries{nullptr};
    const UiSettings* ui{nullptr};
    GLFWwindow* window{nullptr};
    Application* app{nullptr};
};

class BlockInputController {
public:
    void OnMouseButton(MouseButton button, bool pressed, glm::vec2 pos, const BlockInputContext& ctx);
    void OnMouseMove(glm::vec2 pos, glm::vec2 delta, const BlockInputContext& ctx);
    void OnKeyDelete(const BlockInputContext& ctx);
    void Tick(float dt, const BlockInputContext& ctx);

    bool IsRightLookActive() const { return rightLookActive_; }

private:
    const InventoryEntryRef* GetActiveEntry(const BlockInputContext& ctx) const;
    bool ActiveSlotBlocksWorldInteraction(const BlockInputContext& ctx) const;

    void HandleLeftPress(const BlockInputContext& ctx);
    void HandleLeftRelease(float holdSeconds, const BlockInputContext& ctx);
    void HandleRightPress(glm::vec2 pos, const BlockInputContext& ctx);
    void HandleRightRelease(const BlockInputContext& ctx);

    void TryPlaceFromActiveSlot(const BlockInputContext& ctx);
    void TrySpawnCreatureOrSkin(const BlockInputContext& ctx);
    void TryInstantBreak(const BlockInputContext& ctx);

    std::chrono::steady_clock::time_point leftDownTime_{};
    bool leftHeld_{false};
    glm::vec2 rightDownPos_{0.0f};
    bool rightPressed_{false};
    bool rightLookActive_{false};
    bool rightDragExceeded_{false};
};

} // namespace cutum

#endif
