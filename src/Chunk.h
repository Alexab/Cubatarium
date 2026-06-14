#ifndef CHUNK_H
#define CHUNK_H

#include <array>
#include <glm/glm.hpp>
#include "BlockTypes.h"

namespace cutum {

constexpr int CHUNK_SIZE = 16;
constexpr int CHUNK_VOLUME = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;

class UChunk {
public:
 explicit UChunk(glm::ivec3 chunkCoord);

 glm::ivec3 GetCoord() const { return coord_; }
 BlockId GetBlockLocal(glm::ivec3 local) const;
 void SetBlockLocal(glm::ivec3 local, BlockId id);
 bool IsDirty() const { return dirty_; }
 void ClearDirty() { dirty_ = false; }
 void MarkDirty() { dirty_ = true; }

 static int LocalIndex(glm::ivec3 local);

private:
 glm::ivec3 coord_;
 std::array<BlockId, CHUNK_VOLUME> data_{};
 bool dirty_{true};
};

}

#endif
