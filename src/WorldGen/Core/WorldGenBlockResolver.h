#pragma once

#include "World/Math/BlockTypes.h"
#include <string>

namespace cutum
{

class UBlockRegistry;

struct WorldGenBlockResolver
{
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

  void Resolve(UBlockRegistry &registry, const std::string &worldgen_owner_pack_id);
};

/// Pack lookup only — never creates synthetic runtime blocks (scatter worldgen).
BlockId ResolvePackScatterBlockId(UBlockRegistry &registry,
                                  const std::string &worldgen_owner_pack_id,
                                  const std::string &block_name);

} // namespace cutum
