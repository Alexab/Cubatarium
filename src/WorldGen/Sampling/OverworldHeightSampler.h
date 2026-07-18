#pragma once

#include "WorldGen/Sampling/ClimateSampler.h"
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

struct HeightLayerParams
{
  float scale{0.003f};
  int octaves{2};
  float weight{0.63f};
};

struct HeightSampleParams
{
  int octavesBase{4};
  float persistence{0.5f};
  float lacunarity{2.0f};
  float amplitudeBlocks{6.f};
  float detailScale{0.025f};
  float detailWeight{0.05f};
  HeightLayerParams continental;
  HeightLayerParams regional;
  HeightLayerParams detail;
  HeightLayerParams rolling;
  float curveExponent{1.12f};
  int stoneSurfaceAboveY{-1};
  bool useRidgeNoise{false};
  float seaBias{0.45f};
};

/// One evaluation of layered height + climate + surface Y (no duplicate FBM).
struct OverworldHeightSample
{
  float h01{0.f};
  float continental01{0.f};
  float regional01{0.f};
  float detail01{0.f};
  ClimateSample climate{};
  int surfaceY{0};
};

class UOverworldHeightSampler
{
public:
  UOverworldHeightSampler(uint32_t Seed, int SeaLevel, int MaxHeight,
                          HeightPreset preset, float terrainRoughness = 1.0f);

  int SurfaceYAt(int x, int z) const;
  int CoarseSurfaceYAt(int x, int z) const;
  OverworldHeightSample SampleAt(int x, int z) const;
  HeightSampleParams params() const { return Params; }
  HeightPreset preset() const { return Preset; }
  uint32_t seed() const { return Seed; }

private:
  uint32_t Seed;
  int SeaLevel;
  int MaxHeight;
  HeightSampleParams Params;
  HeightPreset Preset;
  float TerrainRoughness{1.0f};
};

float SampleLayeredHeight01(int x, int z, uint32_t seed,
                            const HeightSampleParams &params,
                            HeightPreset preset);
float OverworldMacroHeight01(int x, int z, uint32_t seed);
float SeaBiasForPreset(HeightPreset preset);
int MountainsStoneSurfaceAboveY(int sea_level, int max_height);

} // namespace cutum
