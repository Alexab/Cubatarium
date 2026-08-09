#pragma once

#include <algorithm>

namespace cutum
{

/// Era26 I-O1: under miss+void/VB while moving, allow PendingLight drain
/// (bypass idle-only gate) with caller-capped budget 1–2.
inline bool ShouldDrainPendingLightUnderMissMoving(bool miss, bool moving,
                                                   int void_n, int vb_n,
                                                   int void_T = 200)
{
  if (!moving || !miss)
  {
    return false;
  }
  return void_n > void_T || vb_n > 0 || void_n > 0;
}

/// Era26 I-O1: void Relight collect radius — full focus under void_pressure
/// (do not clamp r≤2 while mh/empty 3–5 sit outside).
inline int VoidRelightCollectRadius(int focus_r, bool miss, bool void_pressure,
                                    bool no_ticket)
{
  const int r = std::max(0, focus_r);
  if (void_pressure || no_ticket || !miss)
  {
    return r;
  }
  return std::min(2, r);
}

/// Era26 I-O3: CollectFullyDark skip only Relight/PendingLight ownership —
/// FirstMesh-only or Dirty-empty must not mask void.
inline bool CollectFullyDarkSkipsOnlyRelightOwnership(bool has_relight_or_pending)
{
  return has_relight_or_pending;
}

/// Era26 I-O2: rim FirstMesh SLA must not clamp away void Relight bg slots.
inline bool ShouldPreserveVoidBgSlotsUnderRimSla(bool rim_sla, bool void_slots)
{
  return rim_sla && void_slots;
}

/// Era26 I-O4: SoftDefer empty + void/dark ⇒ parallel RelightThenMesh beside
/// FirstMesh (kind-separate; Capture stays FM under miss).
inline bool SoftDeferEmptyNeedsParallelVoidRelight(bool empty_placeholder,
                                                   bool fully_dark_or_void)
{
  return empty_placeholder && fully_dark_or_void;
}

/// Era26 I-O5: FillWater lateral (horiz 2–5) remesh/relight band floor.
inline void FillWaterLateralRemeshBand(bool fill_water, int horiz, int sea,
                                       int max_y, int &band_min, int &band_max,
                                       int chunk_size)
{
  if (!fill_water || horiz < 2 || horiz > 5 || chunk_size <= 0)
  {
    return;
  }
  band_min = std::min(band_min, std::max(0, sea - chunk_size));
  band_max = std::max(band_max, std::min(max_y, sea + chunk_size));
}

} // namespace cutum
