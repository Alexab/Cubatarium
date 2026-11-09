#pragma once

namespace cutum
{

struct FluidSurfaceScanTuning
{
  static constexpr int ScanUp = 32;
  static constexpr int ScanDown = 64;
  /// Eye-column query (fog submerged detection): an underwater cave can sit
  /// far below the ocean surface — beyond the slice-builder window. The eye
  /// query runs once per frame on a single column, so a deep window is cheap
  /// and keeps camera_submerged true at any cave depth (water audit
  /// follow-up: underwater caves lost all fog when the surface was more
  /// than ScanUp blocks above the eyes).
  static constexpr int EyeScanUp = 192;
  static constexpr int EyeScanDown = 96;
};

} // namespace cutum
