#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
namespace cutum
{
/// A held action projects the controller's authoritative elapsed time. This
/// carries no inventory and can never complete or consume an action by itself.
struct ItemUseProgress
{
  std::string ItemId;
  uint64_t Instance{0}, Selection{0};
  float Value{0.f};
  void Update(const std::string &id, uint64_t instance, uint64_t selection,
              float elapsed, float duration)
  {
    ItemId = id;
    Instance = instance;
    Selection = selection;
    Value = std::clamp(elapsed / std::max(.001f, duration), .001f, 1.f);
  }
  void Clear() { Value = 0.f; }
  float Phase(const std::string &id, uint64_t instance,
              uint64_t selection) const
  {
    return id == ItemId && instance == Instance && selection == Selection
               ? Value
               : 0.f;
  }
};
/// A transient successful-action event, keyed to the actual source selection.
/// It never stores a stack and is never serialized.
struct ItemActionPulse
{
  std::string ItemId;
  uint64_t Instance{0}, Selection{0};
  float Remaining{0.f}, Duration{.3f};
  void Begin(const std::string &id, uint64_t instance, uint64_t selection)
  {
    ItemId = id;
    Instance = instance;
    Selection = selection;
    Remaining = Duration;
  }
  void Tick(float dt)
  {
    Remaining = std::max(0.f, Remaining - std::max(0.f, dt));
  }
  float Phase(const std::string &id, uint64_t instance,
              uint64_t selection) const
  {
    return Remaining > 0 && id == ItemId && instance == Instance &&
                   selection == Selection
               ? 1.f - Remaining / Duration
               : 0.f;
  }
};
} // namespace cutum
