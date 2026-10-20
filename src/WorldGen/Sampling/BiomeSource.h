#pragma once

#include "WorldGen/Sampling/BiomeSampler.h"

namespace cutum
{

/// Strategy boundary for where surface and 3D biome assignments come from.
/// Implementations own selection policy; the terrain generator consumes the
/// same blend/key contract regardless of source.
class IWorldGenBiomeSource
{
public:
  virtual ~IWorldGenBiomeSource() = default;
  virtual BiomeWeightSet SampleSurfaceWeights(int x, int z, int coarseY,
                                               int seaLevel,
                                               int maxHeight) const = 0;
  virtual BiomeId Sample3D(int x, int y, int z, int surfaceY, int seaLevel,
                           int maxHeight) const = 0;
  virtual BiomeKey Sample3DKey(int x, int y, int z, int surfaceY,
                               int seaLevel, int maxHeight) const = 0;
};

class ProceduralClimateBiomeSource final : public IWorldGenBiomeSource
{
public:
  explicit ProceduralClimateBiomeSource(const UBiomeSampler &sampler)
      : Sampler(sampler)
  {
  }

  BiomeWeightSet SampleSurfaceWeights(int x, int z, int coarseY, int seaLevel,
                                       int maxHeight) const override;
  BiomeId Sample3D(int x, int y, int z, int surfaceY, int seaLevel,
                   int maxHeight) const override;
  BiomeKey Sample3DKey(int x, int y, int z, int surfaceY, int seaLevel,
                       int maxHeight) const override;

private:
  const UBiomeSampler &Sampler;
};

class ImageMapBiomeSource final : public IWorldGenBiomeSource
{
public:
  explicit ImageMapBiomeSource(const UBiomeSampler &caveSampler)
      : CaveSampler(caveSampler)
  {
  }

  BiomeWeightSet SampleSurfaceWeights(int x, int z, int coarseY, int seaLevel,
                                       int maxHeight) const override;
  BiomeId Sample3D(int x, int y, int z, int surfaceY, int seaLevel,
                   int maxHeight) const override;
  BiomeKey Sample3DKey(int x, int y, int z, int surfaceY, int seaLevel,
                       int maxHeight) const override;

private:
  const UBiomeSampler &CaveSampler;
};

} // namespace cutum
