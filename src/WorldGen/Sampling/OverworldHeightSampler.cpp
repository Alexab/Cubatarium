#include "WorldGen/Sampling/OverworldHeightSampler.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Stages/WorldGenStages.h"
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
    p.detailWeight = 0.22f;
    break;
  case HeightPreset::Mountains:
    p.octavesBase = 4;
    p.amplitudeBlocks = 8.0f * scale;
    p.detailWeight = 0.10f;
    p.stoneSurfaceAboveY = -1;
    break;
  case HeightPreset::BetaRetro:
    p.octavesBase = 5;
    p.amplitudeBlocks = 7.0f * scale;
    p.detailWeight = 0.08f;
    p.lacunarity = 2.4f;
    p.useRidgeNoise = true;
    break;
  case HeightPreset::Overworld:
  default:
    p.octavesBase = 4;
    p.amplitudeBlocks = 6.0f * scale;
    p.detailWeight = 0.15f;
    break;
  }
  return p;
}

} // namespace

UOverworldHeightSampler::UOverworldHeightSampler(uint32_t Seed, int SeaLevel,
                                                 int MaxHeight,
                                                 HeightPreset preset,
                                                 float terrainRoughness)
    : Seed(Seed), SeaLevel(SeaLevel), MaxHeight(MaxHeight),
      Params(ParamsForPreset(preset, MaxHeight))
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
  const float sx = static_cast<float>(x) * 0.01f;
  const float sz = static_cast<float>(z) * 0.01f;
  float h = FBM2D(sx, sz, Seed, Params.octavesBase, Params.persistence,
                  Params.lacunarity);
  if (Params.useRidgeNoise)
  {
    h = 1.0f - std::fabs(h);
  }
  const float detail =
      FBM2D(sx * Params.detailScale, sz * Params.detailScale, Seed + 1, 2,
            Params.persistence, Params.lacunarity) *
      Params.detailWeight;
  int surfaceY =
      SeaLevel + static_cast<int>((h + detail - 0.5f) * Params.amplitudeBlocks);
  surfaceY = std::clamp(surfaceY, 1, MaxHeight);
  return surfaceY;
}

int IndevRetroSurfaceY(int x, int z, const ProceduralSettings &settings)
{
  const int base = LegacyHashSurfaceY(x, z, settings);
  const float island = FBM2D(static_cast<float>(x) * 0.02f,
                             static_cast<float>(z) * 0.02f,
                             settings.Seed + 4400, 2, 0.5f, 2.0f);
  if (island > 0.72f)
  {
    const int bump = static_cast<int>((island - 0.72f) * 40.0f);
    return std::clamp(base + bump, 1, settings.MaxHeight);
  }
  return base;
}

} // namespace cutum
