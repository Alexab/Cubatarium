#pragma once

#include "WorldGenContext.h"
#include <cstdint>

namespace cutum {

enum class BiomeId { Plains, Forest, Desert, Hills };

struct BiomeSurfaceRule {
 BlockId surface{BLOCK_AIR};
 BlockId subsurface{BLOCK_AIR};
};

class BiomeSampler {
public:
 explicit BiomeSampler(uint32_t seed);

 BiomeId At(int x, int z, int surfaceY, int seaLevel, int maxHeight) const;
 BiomeSurfaceRule SurfaceRule(BiomeId biome, const WorldGenContext& ctx) const;

private:
 uint32_t seed_;
};

BiomeId ClassifyBiome(float temperature, float moisture, float localHeightNorm);

} // namespace cutum
