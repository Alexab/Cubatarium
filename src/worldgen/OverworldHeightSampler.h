#pragma once

#include <cstdint>

namespace cutum {

enum class HeightPreset { Overworld, Hills, Mountains };

struct HeightSampleParams {
 int octavesBase{4};
 float persistence{0.5f};
 float lacunarity{2.0f};
 float amplitudeBlocks{6.f};
 float detailScale{4.f};
 float detailWeight{0.15f};
 int stoneSurfaceAboveY{-1};
};

class OverworldHeightSampler {
public:
 OverworldHeightSampler(uint32_t seed, int seaLevel, int maxHeight, HeightPreset preset);

 int SurfaceYAt(int x, int z) const;
 HeightSampleParams params() const { return params_; }

private:
 uint32_t seed_;
 int seaLevel_;
 int maxHeight_;
 HeightSampleParams params_;
};

} // namespace cutum
