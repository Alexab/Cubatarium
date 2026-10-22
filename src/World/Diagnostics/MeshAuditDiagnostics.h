#ifndef MESHAUDITDIAGNOSTICS_H
#define MESHAUDITDIAGNOSTICS_H

#include <filesystem>

namespace cutum
{

class UWorld;

/// Oracle for see-through holes: for every resident chunk, compare the
/// brute-force set of exposed opaque top faces in the live voxel data with the
/// top faces produced by a fresh snapshot + mesher run, and record the cache
/// state of chunks that disagree (or whose cached mesh is not drawable).
/// Writes <exe_dir>/mesh_audit.json. Triggered by CUBA_MESH_AUDIT_AFTER_SEC.
class UMeshAuditDiagnostics
{
public:
  static std::filesystem::path DefaultPath();
  /// Returns number of chunks audited, or -1 on failure.
  static int Run(UWorld &world, const std::filesystem::path &path = {});
  /// CUBA_LIGHT_FLICKER_TRACE=1: per-frame sampling of light-related state of
  /// resident chunks near the camera; counts toggles (preview flag, meshed
  /// light revision, published geometry revision, draw gate) per chunk.
  static void TraceFlickerFrame(UWorld &world);
  /// Writes <exe_dir>/light_flicker_trace.json (idempotent).
  static void FinishFlickerTrace();
  /// CUBA_FLIGHT_TELEMETRY=1: every ~2 s append one JSON line to
  /// <exe_dir>/flight_telemetry.jsonl (position, speed, how far ahead columns
  /// are resident/drawable, ring coverage, queue depths, frame time).
  static void TraceFlightTelemetry(UWorld &world);
};

} // namespace cutum

#endif
