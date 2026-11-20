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
  uint64_t frame_epoch{0}, world_epoch{0}, field_incarnation{0},
      field_revision{0};
  uint64_t geometry_incarnation{0}, geometry_content{0};
  bool draw_table{false};
  uint32_t draw_table_buffer{0}, draw_table_row{0}, indirect_buffer{0};
  std::array<uint32_t, 5> indirect_command{};
  std::array<uint32_t, 3> geometry_command{};
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
             << e.handle[3] << ']' << ",\"frame_epoch\":" << e.frame_epoch
             << ",\"world_epoch\":" << e.world_epoch
             << ",\"field_incarnation\":" << e.field_incarnation
             << ",\"field_revision\":" << e.field_revision
             << ",\"geometry_incarnation\":" << e.geometry_incarnation
             << ",\"geometry_content\":" << e.geometry_content
             << ",\"draw_table\":" << (e.draw_table ? "true" : "false")
             << ",\"draw_table_buffer\":" << e.draw_table_buffer
             << ",\"draw_table_row\":" << e.draw_table_row
             << ",\"indirect_buffer\":" << e.indirect_buffer
             << ",\"indirect_command\":[" << e.indirect_command[0] << ','
             << e.indirect_command[1] << ',' << e.indirect_command[2] << ','
             << e.indirect_command[3] << ',' << e.indirect_command[4]
             << "],\"geometry_command\":[" << e.geometry_command[0] << ','
             << e.geometry_command[1] << ',' << e.geometry_command[2] << "]}";
}
} // namespace cutum
