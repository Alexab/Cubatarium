#include "Render/Engine/RenderMeshSink.h"
#include "Render/Engine/GeometryEngine.h"

namespace cutum
{

void URenderMeshSink::Attach(UGeometryEngine *engine) { Engine = engine; }

void URenderMeshSink::OnChunkBlocksChanged(glm::ivec3 /*chunk_coord*/)
{
  ++InvalidationCount;
  if (Engine)
  {
    Engine->InvalidateBlockBatchCache();
  }
}

void URenderMeshSink::OnChunkUnloaded(glm::ivec3 /*chunk_coord*/)
{
  ++InvalidationCount;
  if (Engine)
  {
    Engine->InvalidateBlockBatchCache();
  }
}

} // namespace cutum
