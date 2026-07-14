#ifndef RENDERSETTINGS_H
#define RENDERSETTINGS_H

namespace cutum
{

enum class PerformancePreset
{
  Balanced,
  Fast,
  Quality
};

/// Runtime Render toggles (config.json "Render" section). Use to bisect FPS
/// optimizations.
struct RenderSettings
{
  PerformancePreset Preset{PerformancePreset::Balanced};
  bool GreedyMeshing{true};
  bool AsyncMeshing{true};
  bool FaceQuads{true};
  bool FrustumCulling{true};
  bool BatchCache{true};
  /// Draw creature current/max collision AABB wireframes (in addition to mob
  /// visual).
  bool CreatureDebugBounds{false};
  /// Draw rigid_voxels as textured multi-part cubes (when false, wireframe
  /// fallback per part).
  bool CreatureTexturedParts{true};
  bool CreatureWireframeOverlay{false};
  /// Linear fog at chunk render-distance boundary (greedy shader path).
  bool DistanceFog{true};
  /// Fraction of render distance (blocks) where distance fog begins.
  float DistanceFogStartRatio{0.55f};
  /// Exponent for fog blend; values below 1.0 thicken fog nearer the start.
  float DistanceFogDensity{0.85f};
  /// Use horizontal (XZ) distance for distance fog (classic render fog).
  bool DistanceFogHorizontal{true};
  bool AltitudeAdaptiveFog{true};
  int AltitudeFogThresholdBlocks{32};
  float AltitudeFogPenaltyPer16Blocks{0.05f};
  /// Gradient sky pass before world geometry (greedy path).
  bool GradientSky{true};
  /// View-direction horizon fog in sky shader (vs screen-space band).
  bool HorizonFogRadial{true};
  /// Tint sky horizon fog toward sun/moon direction.
  bool HorizonFogCelestialTint{true};
  /// Blocks subtracted from render horizon for fog end.
  int DistanceFogEndMarginBlocks{12};
  /// Use terrain surface height for altitude-adaptive fog (not feet Y).
  bool AltitudeUseTerrainSurface{true};

  static RenderSettings Legacy()
  {
    RenderSettings s;
    s.GreedyMeshing = false;
    s.FaceQuads = false;
    s.FrustumCulling = false;
    s.BatchCache = false;
    return s;
  }

  /// All Render optimizations on (greedy mesh, face quads, frustum culling,
  /// batch cache).
  static RenderSettings Default() { return RenderSettings{}; }

  /// Greedy merged quads drawn as world-space mesh with baked atlas UV
  /// (requires GreedyMeshing and FaceQuads).
  bool UseFaceQuadDraw() const { return GreedyMeshing && FaceQuads; }

  static RenderSettings FromPreset(PerformancePreset preset);
};

} // namespace cutum

#endif
