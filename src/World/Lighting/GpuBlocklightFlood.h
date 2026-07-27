#pragma once

#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include <cstdint>
#include <glm/glm.hpp>

namespace cutum
{

/// D1.3: blocklight flood entry used by GPU lighting pipeline.
/// Implementation lives in ChunkLighting.cpp (calls internal PropagateBlocklight).
bool TryGpuPropagateBlocklight(UBlockWorld &world, UBlockRegistry &registry,
                               glm::ivec3 chunk_coord);

uint64_t ConsumeGpuBlocklightFloodCount();
void NoteGpuBlocklightFlood();

} // namespace cutum
