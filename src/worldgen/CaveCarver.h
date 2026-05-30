#pragma once

#include <cstdint>

namespace cutum {

struct CaveParams {
 float threshold{0.72f};
 int minY{4};
 int maxDepthBelowSurface{2};
 int octaves{3};
 float persistence{0.5f};
 float lacunarity{2.f};
 float scale{0.05f};
};

bool ShouldCarve(int x, int y, int z, int surfaceY, uint32_t seed, const CaveParams& params);

void CarveColumnCaves(struct WorldGenContext& ctx, int x, int z, int surfaceY, uint32_t seed,
    const CaveParams& params);

} // namespace cutum
