#pragma once

#include <cstdint>

namespace cutum
{

/// Ground-column visual emerge lifecycle (Minecraft/Sodium-style ready gate).
enum class ColumnEmergeState : uint8_t
{
  Empty = 0,
  Generating,
  VoxelsReady,
  Lighting,
  LitReady,
  Meshing,
  RenderReady,
};

} // namespace cutum
