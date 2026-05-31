#pragma once

#include "ProceduralSettings.h"
#include "BlockTypes.h"
#include <glm/glm.hpp>
#include <unordered_set>

namespace cutum {

class BlockWorld;
class BlockRegistry;
class PrefabLibrary;
class ChunkMeshCache;
class ChunkManager;

struct WorldGenContext {
 BlockWorld& world;
 BlockRegistry& registry;
 ProceduralSettings settings;
 PrefabLibrary* prefabs{nullptr};
 ChunkMeshCache* meshCache{nullptr};

 BlockId bedrock{BLOCK_AIR};
 BlockId stone{BLOCK_AIR};
 BlockId dirt{BLOCK_AIR};
 BlockId grass{BLOCK_AIR};
 BlockId sand{BLOCK_AIR};
 BlockId sandstone{BLOCK_AIR};
 BlockId wood{BLOCK_AIR};
 BlockId gravel{BLOCK_AIR};
 BlockId snow{BLOCK_AIR};
 BlockId clay{BLOCK_AIR};
 BlockId ice{BLOCK_AIR};
 BlockId hellrock{BLOCK_AIR};
 BlockId water{BLOCK_AIR};
 BlockId lava{BLOCK_AIR};
 BlockId fire{BLOCK_AIR};

 void ResolveBlockIds();
 void MarkDirtyColumn(int worldX, int worldZ, int minY, int maxY) const;
};

} // namespace cutum
