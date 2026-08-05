#include "Render/Pipeline/GpuTransparentSort.h"

namespace cutum
{

bool TryGpuSortTransparentGreedyBatches(
    std::vector<GreedyBatchRef> & /*refs*/, const UChunkMeshCache & /*cache*/,
    const glm::vec3 & /*cameraPos*/, const UBlockRegistry & /*registry*/)
{
  return false;
}

uint64_t ConsumeGpuTransparentSortCount() { return 0; }

void NoteGpuTransparentSortReadback() {}

uint64_t ConsumeGpuTransparentSortReadbackCount() { return 0; }

} // namespace cutum
