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
class UChunkMeshCache;
class UChunkManager;

struct WorldGenContext
{
  UBlockWorld &World;
  UBlockRegistry &Registry;
  ProceduralSettings Settings;
  UPrefabLibrary *Prefabs{nullptr};
  UChunkMeshCache *MeshCache{nullptr};

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

  void ResolveBlockIds();
  void MarkDirtyColumn(int world_x, int world_z, int min_y, int max_y) const;
};

} // namespace cutum
