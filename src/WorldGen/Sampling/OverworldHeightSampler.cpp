#include "WorldGen/Sampling/OverworldHeightSampler.h"
#include "WorldGen/Core/Noise.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{

HeightSampleParams ParamsForPreset(HeightPreset preset, int maxHeight)
{
  HeightSampleParams p;
  const float scale =
      maxHeight > 15 ? static_cast<float>(maxHeight) / 12.0f : 1.0f;
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

UOverworldHeightSampler::UOverworldHeightSampler(uint32_t seed, int seaLevel,
                                                 int maxHeight,
                                                 HeightPreset preset)
    : seed_(seed), seaLevel_(seaLevel), maxHeight_(maxHeight),
      params_(ParamsForPreset(preset, maxHeight))
{
  if (preset == HeightPreset::Mountains)
  {
    params_.stoneSurfaceAboveY =
        seaLevel +
        static_cast<int>(12.0f * (maxHeight > 15 ? maxHeight / 96.0f : 1.0f));
  }
}

int UOverworldHeightSampler::SurfaceYAt(int x, int z) const
{
  const float sx = static_cast<float>(x) * 0.01f;
  const float sz = static_cast<float>(z) * 0.01f;
  const float h = FBM2D(sx, sz, seed_, params_.octavesBase, params_.persistence,
                        params_.lacunarity);
  const float detail =
      FBM2D(sx * params_.detailScale, sz * params_.detailScale, seed_ + 1, 2,
            params_.persistence, params_.lacunarity) *
      params_.detailWeight;
  int surfaceY = seaLevel_ + static_cast<int>((h + detail - 0.5f) *
                                              params_.amplitudeBlocks);
  surfaceY = std::clamp(surfaceY, 1, maxHeight_);
  return surfaceY;
}

} // namespace cutum
