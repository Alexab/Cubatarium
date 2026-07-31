#pragma once

#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include <cstdint>
#include <glm/glm.hpp>

namespace cutum
{

/// D1.3 / GPF4: blocklight flood for GPU lighting pipeline (CPU authoritative;
/// optional compute dispatch without readback under env flag).
bool TryGpuPropagateBlocklight(UBlockWorld &world, UBlockRegistry &registry,
                               glm::ivec3 chunk_coord);

uint64_t ConsumeGpuBlocklightFloodCount();
void NoteGpuBlocklightFlood();

} // namespace cutum
