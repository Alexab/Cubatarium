#pragma once
#include "Core/Jobs/JobThreadPool.h"
#include "WorldGen/Core/IUChunkPopulator.h"

namespace cutum
{
// The caller owns admission/InFlight and keeps populator/completed alive
// through pool join. ChunkPopulateResult transfers buffers,not content pins.
inline void EnqueueCooperativePopulate(UJobThreadPool &pool,
    IUChunkPopulator *populator,const ChunkPopulateRequest &request,
    UCompletedJobQueue<ChunkPopulateResult> *completed)
{
  pool.Enqueue([populator,request=ChunkPopulateRequest(request),completed] {
    ChunkPopulateResult result=populator->Populate(request);
    completed->Push(std::move(result));
  });
}
} // namespace cutum
