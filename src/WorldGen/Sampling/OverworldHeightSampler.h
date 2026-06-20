#pragma once

#include <cstdint>

namespace cutum
{

enum class HeightPreset
{
  Overworld,
  Hills,
  Mountains,
  BetaRetro,
};

struct HeightSampleParams
{
  int octavesBase{4};
  float persistence{0.5f};
  float lacunarity{2.0f};
  float amplitudeBlocks{6.f};
  float detailScale{4.f};
  float detailWeight{0.15f};
  int stoneSurfaceAboveY{-1};
  bool useRidgeNoise{false};
};

class UOverworldHeightSampler
{
public:
  UOverworldHeightSampler(uint32_t Seed, int SeaLevel, int MaxHeight,
                          HeightPreset preset, float terrainRoughness = 1.0f);

  int SurfaceYAt(int x, int z) const;
  HeightSampleParams params() const { return Params; }

private:
  uint32_t Seed;
  int SeaLevel;
  int MaxHeight;
  HeightSampleParams Params;
};

} // namespace cutum
