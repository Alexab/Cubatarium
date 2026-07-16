#pragma once

#include "WorldGen/Core/WorldGenRefs.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Core/WorldGenContentPinTls.h"
#include "WorldGen/Features/ObjectFeatureConfig.h"
#include <memory>

namespace cutum
{

/// Immutable content handle captured on the main/scheduler thread for a
/// Populate (or similar) job. Workers pin this for the duration of the job so
/// hot-reload cannot tear pack/feature/refs mid-chunk.
struct WorldGenContentSnapshot
{
  std::shared_ptr<const WorldGenPack> Pack;
  std::shared_ptr<const ObjectFeatureConfig> Features;
  std::shared_ptr<const WorldGenRefsCatalog> Refs;
};

WorldGenContentSnapshot CaptureWorldGenContentSnapshot();

/// RAII pin for worker threads. Nested pins are not supported.
class WorldGenContentPinScope
{
public:
  explicit WorldGenContentPinScope(WorldGenContentSnapshot snapshot);
  ~WorldGenContentPinScope();

  WorldGenContentPinScope(const WorldGenContentPinScope &) = delete;
  WorldGenContentPinScope &operator=(const WorldGenContentPinScope &) = delete;

  const WorldGenContentSnapshot &Get() const { return Snapshot; }

private:
  WorldGenContentSnapshot Snapshot;
};

} // namespace cutum
