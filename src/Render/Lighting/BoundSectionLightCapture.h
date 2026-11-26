#pragma once
#include "World/Lighting/SectionLightVolume.h"
#include <glm/glm.hpp>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace cutum {
struct BoundSectionLightCapture
{
  bool captured{false}, valid{false}, cpu_matches{false};
  uint32_t program{0}, atlas_texture{0}, table_texture{0};
  std::array<uint32_t, 4> handle{};
  uint64_t frame_epoch{0}, world_epoch{0}, field_incarnation{0},
      field_revision{0};
  uint64_t geometry_incarnation{0}, geometry_content{0};
  bool draw_table{false};
  uint32_t draw_table_buffer{0}, draw_table_row{0}, indirect_buffer{0};
  std::array<uint32_t, 5> indirect_command{};
  std::array<uint32_t, 3> geometry_command{};
  glm::ivec3 origin{0};
  SectionLightVolume data;

  std::optional<uint8_t> SampleSurface(glm::vec3 point, int face) const
  {
    if (!valid || face < 0 || face > 5)
      return std::nullopt;
    glm::ivec3 cell;
    for (int a = 0; a < 3; ++a)
    {
      if (!std::isfinite(point[a]) || point[a] < -2147483000.0f ||
          point[a] > 2147483000.0f)
        return std::nullopt;
      cell[a] = int(std::floor(point[a] + .5f));
    }
    const int axis =
        (face == 0 || face == 2) ? 2 : ((face == 1 || face == 3) ? 0 : 1);
    cell[axis] = int(std::floor(point[axis] + 1.0f)) -
                 ((face == 2 || face == 3 || face == 5) ? 1 : 0);
    glm::ivec3 local;
    for (int a = 0; a < 3; ++a)
    {
      const int64_t delta = int64_t(cell[a]) - origin[a];
      if (delta < -SectionLightVolume::Radius ||
          delta >= CHUNK_SIZE + SectionLightVolume::Radius)
        return std::nullopt;
      local[a] = int(delta);
    }
    return data.cells[SectionLightVolume::Index(local.x, local.y, local.z)];
  }
};

} // namespace cutum
