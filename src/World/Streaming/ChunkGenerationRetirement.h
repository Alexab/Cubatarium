#pragma once
#include "World/Chunks/ChunkLoadScheduler.h"

namespace cutum {
// The owner and its token/content graph stay alive while this returns false.
// A retry must reuse these same source slots; no retired/detached pair is made.
// The caller clears requested only after publishing a complete successor or
// explicitly cancelling replacement (for example, another background pause).
template<class Populator>
bool TryRetireChunkGeneration(std::unique_ptr<UChunkLoadScheduler> &scheduler,
                             std::unique_ptr<Populator> &populator,bool &requested)
{
  if(scheduler){
    if(!requested){
      scheduler->CancelAllPending(std::chrono::milliseconds(0),"scheduler_reinitialized");
      requested=true;
    }
    if(!scheduler->WaitForWorkersIdle(std::chrono::milliseconds(0)))return false;
    scheduler.reset();
  }
  populator.reset();
  return true;
}
}
