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
    p.detailWeight = 0.22f;
    break;
  case HeightPreset::Mountains:
    p.octavesBase = 4;
    p.amplitudeBlocks = 8.0f * scale;
    p.detailWeight = 0.10f;
    p.stoneSurfaceAboveY = -1;
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
                                                 HeightPreset preset)
    : Seed(Seed), SeaLevel(SeaLevel), MaxHeight(MaxHeight),
      Params(ParamsForPreset(preset, MaxHeight))
{
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
  const float h = FBM2D(sx, sz, Seed, Params.octavesBase, Params.persistence,
                        Params.lacunarity);
  const float detail =
      FBM2D(sx * Params.detailScale, sz * Params.detailScale, Seed + 1, 2,
            Params.persistence, Params.lacunarity) *
      Params.detailWeight;
  int surfaceY =
      SeaLevel + static_cast<int>((h + detail - 0.5f) * Params.amplitudeBlocks);
  surfaceY = std::clamp(surfaceY, 1, MaxHeight);
  return surfaceY;
}

} // namespace cutum
