#ifndef RENDERSETTINGS_H
#define RENDERSETTINGS_H

namespace cutum {

/// Runtime render toggles (config.json "render" section). Use to bisect FPS optimizations.
struct RenderSettings {
 bool greedyMeshing{true};
 bool faceQuads{true};
 bool frustumCulling{true};
 bool batchCache{true};
 /// Draw creature current/max collision AABB wireframes (in addition to mob visual).
 bool creatureDebugBounds{false};

 static RenderSettings Legacy() {
  RenderSettings s;
  s.greedyMeshing = false;
  s.faceQuads = false;
  s.frustumCulling = false;
  s.batchCache = false;
  return s;
 }

 /// All render optimizations on (greedy mesh, face quads, frustum culling, batch cache).
 static RenderSettings Default() { return RenderSettings{}; }

 /// Greedy merged quads drawn as world-space mesh with baked atlas UV (requires greedyMeshing and faceQuads).
 bool UseFaceQuadDraw() const { return greedyMeshing && faceQuads; }
};

}

#endif
