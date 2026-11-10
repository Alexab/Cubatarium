#pragma once

#include "Render/Mesh/ChunkMeshCache.h"
#include "Blocks/BlockRegistry.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

/// GPF2: GPU bitonic sort for transparent GreedyBatchRef order (desktop GL).
bool TryGpuSortTransparentGreedyBatches(
    std::vector<GreedyBatchRef> &refs, const UChunkMeshCache &cache,
    const glm::vec3 &cameraPos, const UBlockRegistry &registry);

uint64_t ConsumeGpuTransparentSortCount();

/// Incremented on each keys SSBO glGetBufferSubData (sync readback).
void NoteGpuTransparentSortReadback();
uint64_t ConsumeGpuTransparentSortReadbackCount();

} // namespace cutum
