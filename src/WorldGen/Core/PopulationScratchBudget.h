#pragma once
#include "Core/Memory/BufferAllocationBudget.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Chunks/Chunk.h"
#include <stdexcept>

namespace cutum
{
// One cleared scratch manager, owned by one Populate call. Keep conservative
// payload credit until all scratch chunks/fields have actually been destroyed.
class UPopulationScratchBudget
{
public:
  UPopulationScratchBudget(UChunkManager &manager,BufferBudgetContext context)
      : Manager(manager),Context(std::move(context))
  {
    if(Manager.GetResidentChunkCount()!=0 || Manager.GetFreeListSize()!=0)
      throw std::logic_error("Population scratch budget requires a cleared manager");
    Manager.SetBeforeChunkAllocationFn([this] {
      if(!Context.budget) return;
      constexpr size_t bytes=sizeof(UChunk)+sizeof(SectionLightField);
      Context.budget->Reserve(*Context.job,bytes);
      Credit+=bytes;
    });
  }
  ~UPopulationScratchBudget() {
    Manager.SetBeforeChunkAllocationFn({});
    Manager.Clear();
    if(Context.budget && Credit) Context.budget->Release(*Context.job,Credit);
  }
  UPopulationScratchBudget(const UPopulationScratchBudget &)=delete;
  UPopulationScratchBudget &operator=(const UPopulationScratchBudget &)=delete;
private:
  UChunkManager &Manager;
  BufferBudgetContext Context;
  size_t Credit{0};
};
} // namespace cutum
