#pragma once

namespace cutum
{

struct WorldGenContext;

void ApplyMudflowToChunk(WorldGenContext &ctx, int base_x, int base_z,
                         int iterations, int *out_voxels_moved = nullptr);

} // namespace cutum
