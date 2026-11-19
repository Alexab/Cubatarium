#pragma once
#include <array>
#include <cstdint>
#include <ostream>

namespace cutum
{
// POD retained by the bounded visual trace ring. Full GPU volumes stay only
// in the render-frame's bounded packed-range witnesses.
struct BoundFieldPixelEvidence
{
  bool requested{false}, captured{false}, valid{false}, cpu_matches{false};
  uint8_t packed{0};
  uint32_t program{0}, atlas_texture{0}, table_texture{0};
  std::array<uint32_t, 4> handle{};
  uint64_t world_epoch{0}, field_incarnation{0}, field_revision{0};
  uint64_t geometry_incarnation{0}, geometry_content{0};
};
inline std::ostream &operator<<(std::ostream &out,
                                const BoundFieldPixelEvidence &e)
{
  return out << "{\"schema\":\"bound-section-light-pixel.v1\",\"requested\":"
             << (e.requested ? "true" : "false")
             << ",\"captured\":" << (e.captured ? "true" : "false")
             << ",\"valid\":" << (e.valid ? "true" : "false")
             << ",\"cpu_matches\":" << (e.cpu_matches ? "true" : "false")
             << ",\"packed\":" << unsigned(e.packed)
             << ",\"program\":" << e.program
             << ",\"atlas_texture\":" << e.atlas_texture
             << ",\"table_texture\":" << e.table_texture << ",\"handle\":["
             << e.handle[0] << ',' << e.handle[1] << ',' << e.handle[2] << ','
             << e.handle[3] << ']' << ",\"world_epoch\":" << e.world_epoch
             << ",\"field_incarnation\":" << e.field_incarnation
             << ",\"field_revision\":" << e.field_revision
             << ",\"geometry_incarnation\":" << e.geometry_incarnation
             << ",\"geometry_content\":" << e.geometry_content << '}';
}
} // namespace cutum
