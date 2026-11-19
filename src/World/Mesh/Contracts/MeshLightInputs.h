#pragma once

#include <cstdint>

namespace cutum
{

enum class MeshLightInputs : uint8_t
{
  Baked,
  /// Geometry has zero baked attributes. Publication must independently
  /// prepare a compatible calculated physical-field lease.
  PhysicalField,
};

} // namespace cutum
