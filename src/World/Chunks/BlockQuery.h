#pragma once

#include "World/Math/BlockTypes.h"
#include <cstdint>

namespace cutum
{

/// Phase 4: voxel query SoT — missing chunk is Unloaded, not AIR.
enum class BlockQueryKind : uint8_t
{
  Air = 0,
  Solid = 1,
  Unloaded = 2,
};

struct BlockQueryResult
{
  BlockQueryKind kind{BlockQueryKind::Unloaded};
  BlockId id{BLOCK_AIR};

  bool IsUnloaded() const { return kind == BlockQueryKind::Unloaded; }
  bool IsAir() const { return kind == BlockQueryKind::Air; }
  bool IsSolid() const { return kind == BlockQueryKind::Solid; }
  /// Legacy callers that treated missing as air — prefer explicit checks.
  bool IsAirOrUnloaded() const
  {
    return kind == BlockQueryKind::Air || kind == BlockQueryKind::Unloaded;
  }
};

inline BlockQueryResult MakeUnloadedQuery()
{
  return BlockQueryResult{BlockQueryKind::Unloaded, BLOCK_AIR};
}

inline BlockQueryResult MakeAirQuery()
{
  return BlockQueryResult{BlockQueryKind::Air, BLOCK_AIR};
}

inline BlockQueryResult MakeSolidQuery(BlockId id)
{
  if (id == BLOCK_AIR)
  {
    return MakeAirQuery();
  }
  return BlockQueryResult{BlockQueryKind::Solid, id};
}

} // namespace cutum
