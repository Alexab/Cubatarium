#include "WorldGen/Sampling/OverworldHeightSampler.h"
#include "WorldGen/Core/Noise.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{

HeightSampleParams ParamsForPreset(HeightPreset preset, int MaxHeight)
{
  HeightSampleParams p;
  const float scale =
      MaxHeight > 15 ? static_cast<float>(MaxHeight) / 12.0f : 1.0f;
  switch (preset)
  {
  case HeightPreset::Hills:
    p.octavesBase = 5;
    p.amplitudeBlocks = 4.0f * scale;
    p.detailWeight = 0.18f;
    p.seaBias = 0.46f;
    break;
  case HeightPreset::Mountains:
    p.octavesBase = 4;
    p.amplitudeBlocks = 7.0f * scale;
    p.detailWeight = 0.08f;
    p.stoneSurfaceAboveY = -1;
    p.seaBias = 0.44f;
    break;
  case HeightPreset::BetaRetro:
    p.octavesBase = 5;
    p.amplitudeBlocks = 6.0f * scale;
    p.detailWeight = 0.08f;
    p.lacunarity = 2.4f;
    p.useRidgeNoise = true;
    p.seaBias = 0.46f;
    break;
  case HeightPreset::Overworld:
  default:
    p.octavesBase = 4;
    p.amplitudeBlocks = 4.5f * scale;
    p.detailWeight = 0.12f;
    p.seaBias = 0.45f;
    break;
  }
  return p;
}

} // namespace

float SeaBiasForPreset(HeightPreset preset)
{
  return ParamsForPreset(preset, 128).seaBias;
}

float SampleLayeredHeight01(int x, int z, uint32_t seed,
                            const HeightSampleParams &params,
                            HeightPreset preset)
{
  const float wx = static_cast<float>(x);
  const float wz = static_cast<float>(z);

  float continental =
      NormalizedFBM2D(wx * 0.003f, wz * 0.003f, seed, 2, params.persistence,
                      params.lacunarity);
  float regional =
      NormalizedFBM2D(wx * 0.010f, wz * 0.010f, seed + 10, 4, params.persistence,
                      params.lacunarity);
  float detail =
      NormalizedFBM2D(wx * 0.040f, wz * 0.040f, seed + 20, 2, params.persistence,
                      params.lacunarity);

  if (preset == HeightPreset::BetaRetro && params.useRidgeNoise)
  {
    regional = 1.0f - std::fabs(regional);
  }

  continental = (continental + 1.0f) * 0.5f;
  regional = (regional + 1.0f) * 0.5f;
  detail = (detail + 1.0f) * 0.5f;

  float h01 = 0.55f * continental + 0.35f * regional + 0.10f * detail;
  h01 = std::clamp(h01, 0.0f, 1.0f);
  if (preset == HeightPreset::Overworld)
  {
    h01 = std::pow(h01, 1.08f);
  }
  return h01;
}

UOverworldHeightSampler::UOverworldHeightSampler(uint32_t Seed, int SeaLevel,
                                                 int MaxHeight,
                                                 HeightPreset preset,
                                                 float terrainRoughness)
    : Seed(Seed), SeaLevel(SeaLevel), MaxHeight(MaxHeight),
      Params(ParamsForPreset(preset, MaxHeight)), Preset(preset)
{
  const float roughness = std::max(0.25f, terrainRoughness);
  Params.amplitudeBlocks *= roughness;
  Params.detailWeight *= roughness;
  if (preset == HeightPreset::Mountains)
  {
    Params.stoneSurfaceAboveY =
        SeaLevel +
        static_cast<int>(12.0f * (MaxHeight > 15 ? MaxHeight / 96.0f : 1.0f));
  }
}

int UOverworldHeightSampler::SurfaceYAt(int x, int z) const
{
  const float h01 = SampleLayeredHeight01(x, z, Seed, Params, Preset);
  const float delta = (h01 - Params.seaBias) * Params.amplitudeBlocks;
  int surfaceY = SeaLevel + static_cast<int>(std::lround(delta));
  surfaceY = std::clamp(surfaceY, 1, MaxHeight);
  return surfaceY;
}

} // namespace cutum
