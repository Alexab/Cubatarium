#ifndef RENDERSETTINGS_H
#define RENDERSETTINGS_H

namespace cutum
{

/// Runtime Render toggles (config.json "Render" section). Use to bisect FPS
/// optimizations.
struct RenderSettings
{
  bool GreedyMeshing{true};
  bool AsyncMeshing{false};
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
};

} // namespace cutum

#endif
