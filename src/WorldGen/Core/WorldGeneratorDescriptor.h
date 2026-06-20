#pragma once

#include "WorldGen/Core/IWorldGenPipeline.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include <cstddef>
#include <cstdint>
#include <memory>

namespace cutum
{

enum WorldGenFeatureFlags : uint32_t
{
  kFeatureFlatSurfaceY = 1u << 0,
  kFeatureCaves = 1u << 1,
  kFeatureTrees = 1u << 2,
  kFeatureBiomes = 1u << 3,
  kFeatureStructures = 1u << 4,
  kFeatureFluids = 1u << 5,
  kFeatureTuning = 1u << 6,
  kFeatureVertical = 1u << 7,
  kFeatureOres = 1u << 8,
};

struct WorldGeneratorDescriptor
{
  ProceduralGenerator Id;
  const char *DisplayName;
  const char *Description;
  uint32_t FeatureFlags;
  void (*ApplyDefaults)(ProceduralSettings &settings);
  std::unique_ptr<IWorldGenPipeline> (*Create)(WorldGenContext ctx);
};

class UWorldGeneratorRegistry
{
public:
  static size_t Count();
  static const WorldGeneratorDescriptor &Get(size_t index);
  static const WorldGeneratorDescriptor *Find(ProceduralGenerator id);
  static int IndexOf(ProceduralGenerator id);
  static std::unique_ptr<IWorldGenPipeline> Create(WorldGenContext ctx);
};

} // namespace cutum
