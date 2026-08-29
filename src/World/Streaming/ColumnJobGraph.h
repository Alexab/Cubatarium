#pragma once

#include <cstdint>

namespace cutum
{

/// FP4 / ARCHOPTS V4 slice: explicit column job stages (single scheduler SoT).
enum class ColumnJobStage : uint8_t
{
  Absent = 0,
  Gen,
  PendingLight,
  LitReady,
  Meshing,
  GpuPending,
  RenderReady,
};

/// Priority band for distance-prioritized work queue.
enum class ColumnJobPriority : uint8_t
{
  Background = 0,
  KeepRing,
  FovRing,
  Underfeet,
  Edit,
};

inline const char *ColumnJobStageName(ColumnJobStage s)
{
  switch (s)
  {
  case ColumnJobStage::Absent: return "Absent";
  case ColumnJobStage::Gen: return "Gen";
  case ColumnJobStage::PendingLight: return "PendingLight";
  case ColumnJobStage::LitReady: return "LitReady";
  case ColumnJobStage::Meshing: return "Meshing";
  case ColumnJobStage::GpuPending: return "GpuPending";
  case ColumnJobStage::RenderReady: return "RenderReady";
  }
  return "Unknown";
}

/// Derive next stage from column truth (scheduler-driven, not event zoo).
inline ColumnJobStage DeriveColumnJobStage(bool has_chunk, bool pending_light,
                                           bool lit_ready, bool meshing,
                                           bool gpu_pending, bool render_ready)
{
  if (!has_chunk)
  {
    return ColumnJobStage::Absent;
  }
  if (render_ready)
  {
    return ColumnJobStage::RenderReady;
  }
  if (gpu_pending)
  {
    return ColumnJobStage::GpuPending;
  }
  if (meshing)
  {
    return ColumnJobStage::Meshing;
  }
  if (pending_light)
  {
    return ColumnJobStage::PendingLight;
  }
  if (lit_ready)
  {
    return ColumnJobStage::LitReady;
  }
  return ColumnJobStage::Gen;
}

} // namespace cutum
