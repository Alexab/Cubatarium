#ifndef RENDERSETTINGS_H
#define RENDERSETTINGS_H

namespace cutum {

/// Runtime render toggles (config.json "render" section). Use to bisect FPS optimizations.
struct RenderSettings {
 bool greedyMeshing{false};
 bool faceQuads{false};
 bool frustumCulling{false};
 bool batchCache{false};

 static RenderSettings Legacy() { return RenderSettings{}; }

 /// Greedy merged quads + instanced face draw (requires both greedyMeshing and faceQuads).
 bool UseFaceQuadDraw() const { return greedyMeshing && faceQuads; }
};

}

#endif
