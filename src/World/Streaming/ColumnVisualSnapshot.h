#pragma once

namespace cutum
{

/// FP-C1: single per-epoch visual truth for VB / unfinished / softdefer.
struct ColumnVisualSnapshot
{
  int visible_black_focus_n{0};
  int visible_black_no_ticket_n{0};
  int unfinished_visual{0};
  int softdefer_empty_near_n{0};
  int column_loaded_no_mesh_n{0};
};

} // namespace cutum
