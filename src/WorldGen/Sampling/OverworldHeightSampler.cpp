#include "WorldGen/Sampling/OverworldHeightSampler.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Sampling/ClimateSampler.h"
#include "WorldGen/Sampling/TerrainClimateMapper.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{

void ApplyPackHeightLayers(HeightSampleParams &p)
{
  const PackHeightConfig &pack = UWorldGenPack::HeightConfig();
  if (!pack.Loaded)
  {
    return;
  }
  p.continental.scale = pack.Continental.Scale;
  p.continental.octaves = pack.Continental.Octaves;
  p.continental.weight = pack.Continental.Weight;
  p.regional.scale = pack.Regional.Scale;
  p.regional.octaves = pack.Regional.Octaves;
  p.regional.weight = pack.Regional.Weight;
  p.detail.scale = pack.Detail.Scale;
  p.detail.octaves = pack.Detail.Octaves;
  p.detail.weight = pack.Detail.Weight;
  p.detailWeight = pack.Detail.Weight;
  p.detailScale = pack.Detail.Scale;
  p.rolling.scale = pack.Rolling.Scale;
  p.rolling.octaves = pack.Rolling.Octaves;
  p.rolling.weight = pack.Rolling.Weight;
  p.seaBias = pack.SeaBias;
  p.curveExponent = pack.CurveExponent;
}

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
    p.continental = {0.003f, 2, 0.55f};
    p.regional = {0.010f, 4, 0.35f};
    p.detail = {0.040f, 2, 0.10f};
    p.seaBias = 0.46f;
    p.curveExponent = 1.08f;
    break;
  case HeightPreset::Mountains:
    p.octavesBase = 4;
    p.amplitudeBlocks = 7.0f * scale;
    p.detailWeight = 0.08f;
    p.continental = {0.003f, 2, 0.55f};
    p.regional = {0.010f, 4, 0.35f};
    p.detail = {0.040f, 2, 0.08f};
    p.stoneSurfaceAboveY = -1;
    p.seaBias = 0.44f;
    p.curveExponent = 1.08f;
    break;
  case HeightPreset::BetaRetro:
    p.octavesBase = 5;
    p.amplitudeBlocks = 6.0f * scale;
    p.detailWeight = 0.08f;
    p.continental = {0.003f, 2, 0.55f};
    p.regional = {0.010f, 4, 0.35f};
    p.detail = {0.040f, 2, 0.08f};
    p.lacunarity = 2.4f;
    p.useRidgeNoise = true;
    p.seaBias = 0.46f;
    p.curveExponent = 1.08f;
    break;
  case HeightPreset::Overworld:
  default:
    p.octavesBase = 4;
    p.amplitudeBlocks = 4.5f * scale;
    p.detailWeight = 0.05f;
    p.continental = {0.003f, 2, 0.63f};
    p.regional = {0.008f, 3, 0.32f};
    p.detail = {0.025f, 2, 0.05f};
    p.rolling = {0.012f, 3, 0.08f};
    p.seaBias = 0.45f;
    p.curveExponent = 1.12f;
    ApplyPackHeightLayers(p);
    break;
  }
  return p;
}

} // namespace

namespace
{

struct LayeredHeightParts
{
  float h01{0.f};
  float continental01{0.f};
  float regional01{0.f};
  float detail01{0.f};
};

LayeredHeightParts SampleLayeredHeightParts(int x, int z, uint32_t seed,
                                            const HeightSampleParams &params,
                                            HeightPreset preset)
{
  LayeredHeightParts out;
  const float wx = static_cast<float>(x);
  const float wz = static_cast<float>(z);

  const int regionalOctaves =
      preset == HeightPreset::Overworld
          ? params.regional.octaves
          : std::max(2, params.octavesBase);
  const int continentalOctaves =
      preset == HeightPreset::Overworld
          ? params.continental.octaves
          : std::max(2, params.octavesBase - 2);
  const int detailOctaves =
      preset == HeightPreset::Overworld ? params.detail.octaves : 2;

  float continental = NormalizedFBM2D(
      wx * params.continental.scale, wz * params.continental.scale, seed,
      continentalOctaves, params.persistence, params.lacunarity);
  float regional = NormalizedFBM2D(
      wx * params.regional.scale, wz * params.regional.scale, seed + 10,
      regionalOctaves, params.persistence, params.lacunarity);
  float detail = NormalizedFBM2D(wx * params.detail.scale, wz * params.detail.scale,
                                 seed + 20, detailOctaves, params.persistence,
                                 params.lacunarity);

  if (preset == HeightPreset::BetaRetro && params.useRidgeNoise)
  {
    regional = 1.0f - std::fabs(regional);
  }

  continental = (continental + 1.0f) * 0.5f;
  regional = (regional + 1.0f) * 0.5f;
  detail = (detail + 1.0f) * 0.5f;

  const float detailW =
      preset == HeightPreset::Overworld ? params.detail.weight : params.detailWeight;
  float h01 = params.continental.weight * continental +
              params.regional.weight * regional + detailW * detail;
  h01 = std::clamp(h01, 0.0f, 1.0f);
  if (preset == HeightPreset::Overworld || preset == HeightPreset::Hills)
  {
    h01 = std::pow(h01, params.curveExponent);
  }
  out.h01 = h01;
  out.continental01 = continental;
  out.regional01 = regional;
  out.detail01 = detail;
  return out;
}

} // namespace

float SeaBiasForPreset(HeightPreset preset)
{
  HeightSampleParams params;
  const float scale = 128.0f / 12.0f;
  params.octavesBase = 4;
  params.amplitudeBlocks = 4.5f * scale;
  params.detailWeight = 0.05f;
  params.continental = {0.003f, 2, 0.63f};
  params.regional = {0.008f, 3, 0.32f};
  params.detail = {0.025f, 2, 0.05f};
  params.seaBias = 0.45f;
  params.curveExponent = 1.12f;
  const PackHeightConfig &pack = UWorldGenPack::HeightConfig();
  if (pack.Loaded)
  {
    params.seaBias = pack.SeaBias;
  }
  (void)preset;
  return params.seaBias;
}

float SampleLayeredHeight01(int x, int z, uint32_t seed,
                            const HeightSampleParams &params,
                            HeightPreset preset)
{
  return SampleLayeredHeightParts(x, z, seed, params, preset).h01;
}

float OverworldMacroHeight01(int x, int z, uint32_t seed)
{
  HeightSampleParams params;
  const float scale = 128.0f / 12.0f;
  params.octavesBase = 4;
  params.amplitudeBlocks = 4.5f * scale;
  params.detailWeight = 0.05f;
  params.continental = {0.003f, 2, 0.63f};
  params.regional = {0.008f, 3, 0.32f};
  params.detail = {0.025f, 2, 0.05f};
  params.seaBias = 0.45f;
  params.curveExponent = 1.12f;
  const PackHeightConfig &pack = UWorldGenPack::HeightConfig();
  if (pack.Loaded)
  {
    params.continental.scale = pack.Continental.Scale;
    params.continental.octaves = pack.Continental.Octaves;
    params.continental.weight = pack.Continental.Weight;
    params.regional.scale = pack.Regional.Scale;
    params.regional.octaves = pack.Regional.Octaves;
    params.regional.weight = pack.Regional.Weight;
    params.detail.scale = pack.Detail.Scale;
    params.detail.octaves = pack.Detail.Octaves;
    params.detail.weight = pack.Detail.Weight;
    params.detailWeight = pack.Detail.Weight;
    params.seaBias = pack.SeaBias;
    params.curveExponent = pack.CurveExponent;
  }
  return SampleLayeredHeight01(x, z, seed, params, HeightPreset::Overworld);
}

UOverworldHeightSampler::UOverworldHeightSampler(uint32_t Seed, int SeaLevel,
                                                 int MaxHeight,
                                                 HeightPreset preset,
                                                 float terrainRoughness)
    : Seed(Seed), SeaLevel(SeaLevel), MaxHeight(MaxHeight),
      Params(ParamsForPreset(preset, MaxHeight)), Preset(preset)
{
  const float roughness = std::max(0.25f, terrainRoughness);
  TerrainRoughness = roughness;
  Params.amplitudeBlocks *= roughness;
  Params.detailWeight *= (2.0f - roughness);
  Params.detail.weight = Params.detailWeight;
  if (preset == HeightPreset::Mountains)
  {
    Params.stoneSurfaceAboveY =
        MountainsStoneSurfaceAboveY(SeaLevel, MaxHeight);
  }
}

int MountainsStoneSurfaceAboveY(int sea_level, int max_height)
{
  return sea_level +
         static_cast<int>(12.0f * (max_height > 15 ? max_height / 96.0f : 1.0f));
}

int UOverworldHeightSampler::CoarseSurfaceYAt(int x, int z) const
{
  return SampleAt(x, z).surfaceY;
}

OverworldHeightSample UOverworldHeightSampler::SampleAt(int x, int z) const
{
  OverworldHeightSample out;
  const LayeredHeightParts layered =
      SampleLayeredHeightParts(x, z, Seed, Params, Preset);
  out.h01 = layered.h01;
  out.continental01 = layered.continental01;
  out.regional01 = layered.regional01;
  out.detail01 = layered.detail01;

  if (Preset == HeightPreset::Overworld)
  {
    out.climate = SampleClimate(x, z, Seed);
    const float offset = ClimateTerrainOffset(
        out.climate, SeaLevel, MaxHeight, out.regional01, out.detail01,
        Params.detailWeight, Params.amplitudeBlocks, TerrainRoughness, x, z,
        Seed, Params.rolling.weight, Params.rolling.scale, Params.rolling.octaves);
    const float delta = (out.h01 - Params.seaBias) * Params.amplitudeBlocks * 0.32f;
    float heightDelta = offset + delta;
    const float maxUp = Params.amplitudeBlocks * 0.62f;
    const float maxDown = Params.amplitudeBlocks * 0.55f;
    heightDelta = std::clamp(heightDelta, -maxDown, maxUp);
    out.surfaceY =
        SeaLevel + static_cast<int>(std::floor(heightDelta + 0.5f));
    out.surfaceY = std::clamp(out.surfaceY, 1, MaxHeight);
    return out;
  }
  const float delta = (out.h01 - Params.seaBias) * Params.amplitudeBlocks;
  out.surfaceY = SeaLevel + static_cast<int>(std::floor(delta + 0.5f));
  out.surfaceY = std::clamp(out.surfaceY, 1, MaxHeight);
  return out;
}

int UOverworldHeightSampler::SurfaceYAt(int x, int z) const
{
  return CoarseSurfaceYAt(x, z);
}

} // namespace cutum
