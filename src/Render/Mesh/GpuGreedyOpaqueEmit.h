#pragma once

#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "Render/Mesh/GreedyMeshBatch.h"
#include "Blocks/BlockRegistry.h"
#include "Render/GlIncludes.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

struct GpuGreedyEmitState
{
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  GLuint MaskProgram{0};
  GLuint GreedyProgram{0};
  GLuint EmitProgram{0};
  GLuint OccSsbo{0};
  GLuint MaskSsbo{0};
  GLuint BlocksSsbo{0};
  GLuint LightsSsbo{0};
  GLuint RectsSsbo{0};
  GLuint CountersSsbo{0};
  GLuint VertSsbo{0};
  GLuint IndexSsbo{0};
#endif
  bool InitAttempted{false};
};

/// GPF1: mask → greedy-rect → vertex/index emit on GPU (desktop GL only).
bool TryGpuOpaqueEmitToBatches(GpuGreedyEmitState &state,
                               const ChunkMeshSnapshot &snapshot,
                               UBlockRegistry &registry, glm::ivec3 coord,
                               std::vector<GreedyMeshBatch> &out_batches);

uint64_t ConsumeGpuOpaqueEmitCount();

} // namespace cutum
