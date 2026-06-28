#pragma once

#include "World/Math/BlockTypes.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenBlockResolver.h"
#include "WorldGen/Core/ColumnWriteContext.h"
#include <functional>
#include <glm/glm.hpp>

namespace cutum
{

class UBlockWorld;
class UBlockRegistry;
class UObjectLibrary;
struct ObjectFeatureConfig;

/// World generation context: blocks and prefab placement only (no creatures).
struct WorldGenContext
{
  UBlockWorld &World;
  UBlockRegistry &Registry;
  ProceduralSettings Settings;
  UObjectLibrary *Objects{nullptr};
  const ObjectFeatureConfig *ObjectFeatures{nullptr};
  std::string WorldgenOwnerPackId;

  WorldGenBlockResolver Blocks;

  WorldGenContext(UBlockWorld &world, UBlockRegistry &registry,
                  ProceduralSettings settings, UObjectLibrary *objects = nullptr);

  void ResolveBlockIds();

  using ColumnMeshDirtyFn =
      std::function<void(int world_x, int world_z, int min_y, int max_y)>;
  ColumnMeshDirtyFn OnColumnMeshDirty;

  void ResetColumnDirty(int world_x, int world_z);
  void AccumulateDirtyColumn(int min_y, int max_y);
  void FlushColumnDirty();

  void MarkDirtyColumn(int world_x, int world_z, int min_y, int max_y) const;

  ColumnWriteContext GetWriteContext();

private:
  mutable bool ColumnDirtyActive{false};
  mutable int ColumnDirtyWorldX{0};
  mutable int ColumnDirtyWorldZ{0};
  mutable int ColumnDirtyMinY{0};
  mutable int ColumnDirtyMaxY{-1};
};

} // namespace cutum
