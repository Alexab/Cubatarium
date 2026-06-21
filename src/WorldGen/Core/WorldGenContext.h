#pragma once

#include "World/Math/BlockTypes.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include <glm/glm.hpp>
#include <unordered_set>

namespace cutum
{

class UBlockWorld;
class UBlockRegistry;
class UPrefabLibrary;
class UChunkManager;

/// World generation context: blocks and prefab placement only (no creatures).
struct WorldGenContext
{
  UBlockWorld &World;
  UBlockRegistry &Registry;
  ProceduralSettings Settings;
  UPrefabLibrary *Prefabs{nullptr};
  /// Primary pack that owns worldgen block definitions (from world_data / config).
  std::string WorldgenOwnerPackId;

  BlockId Bedrock{BLOCK_AIR};
  BlockId Stone{BLOCK_AIR};
  BlockId Dirt{BLOCK_AIR};
  BlockId Grass{BLOCK_AIR};
  BlockId Sand{BLOCK_AIR};
  BlockId Sandstone{BLOCK_AIR};
  BlockId Wood{BLOCK_AIR};
  BlockId Gravel{BLOCK_AIR};
  BlockId Snow{BLOCK_AIR};
  BlockId Clay{BLOCK_AIR};
  BlockId Ice{BLOCK_AIR};
  BlockId Hellrock{BLOCK_AIR};
  BlockId Water{BLOCK_AIR};
  BlockId Lava{BLOCK_AIR};
  BlockId Fire{BLOCK_AIR};
  BlockId OreCoal{BLOCK_AIR};
  BlockId OreIron{BLOCK_AIR};

  WorldGenContext(UBlockWorld &world, UBlockRegistry &registry,
                  ProceduralSettings settings, UPrefabLibrary *prefabs = nullptr);

  void ResolveBlockIds();

  void ResetColumnDirty(int world_x, int world_z);
  void AccumulateDirtyColumn(int min_y, int max_y);
  void FlushColumnDirty();

  void MarkDirtyColumn(int world_x, int world_z, int min_y, int max_y) const;

private:
  mutable bool ColumnDirtyActive{false};
  mutable int ColumnDirtyWorldX{0};
  mutable int ColumnDirtyWorldZ{0};
  mutable int ColumnDirtyMinY{0};
  mutable int ColumnDirtyMaxY{-1};
};

} // namespace cutum
