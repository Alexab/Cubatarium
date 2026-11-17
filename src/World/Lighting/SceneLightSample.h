#pragma once

#include <cstdint>
#include <optional>

namespace cutum
{
enum class SceneLightPolicy
{
  Physical,
  Flat
};

enum class SceneLightState
{
  Missing,
  Transient,
  Computed,
  FlatConstant,
  BoundaryConstant
};

enum class SceneLightConsumer
{
  FirstPersonItem,
  Creature,
  Item
};

/// Main-thread point sample. Constants are intentional rendering policy, not
/// physical calculation receipts. Uncertified bytes are never returned as
/// light.
struct SceneLightSample
{
  SceneLightState state{SceneLightState::Missing};
  std::optional<uint8_t> packed;
  uint64_t field_incarnation{0};
  uint64_t field_revision{0};

  bool IsReady() const { return packed.has_value(); }
};
} // namespace cutum
