#ifndef RENDERSETTINGS_H
#define RENDERSETTINGS_H

namespace cutum {

/// Runtime render toggles (config.json "render" section). Use to bisect FPS optimizations.
struct RenderSettings {
 bool greedyMeshing{false};
 bool faceQuads{false};
 bool frustumCulling{false};
 bool batchCache{false};

 /// Closest to pre-optimization behavior (naive cubes, no cull/cache).
 static RenderSettings Legacy() { return RenderSettings{}; }

 bool UseFaceQuadDraw() const { return greedyMeshing && faceQuads; }
};

}

#endif
