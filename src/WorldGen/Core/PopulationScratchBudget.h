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
      : Manager(manager),Context(std::move(context)),Metadata(manager.GetMetadataAllocationResource())
  {
    if(Manager.GetResidentChunkCount()!=0 || Manager.GetFreeListSize()!=0)
      throw std::logic_error("Population scratch budget requires a cleared manager");
    try {
      Manager.SetBeforeChunkAllocationFn([this] {Reserve(sizeof(UChunk)+sizeof(SectionLightField));});
      Metadata->SetBeforeAllocationFn([this](size_t bytes) {Reserve(bytes);});
    } catch(...) {
      Manager.SetBeforeChunkAllocationFn({}); Metadata->SetBeforeAllocationFn({}); throw;
    }
  }
  ~UPopulationScratchBudget() {
    Manager.SetBeforeChunkAllocationFn({});
    Metadata->SetBeforeAllocationFn({});
    Manager.Clear();
    // Fresh empty sentinels are fixed manager overhead, not active-job debt.
    // Reset after detaching quota callbacks so recovery cannot refuse itself.
    Manager.ReleaseEmptyMetadataStorage();
    if(Context.budget && Credit) Context.budget->Release(*Context.job,Credit);
  }
  UPopulationScratchBudget(const UPopulationScratchBudget &)=delete;
  UPopulationScratchBudget &operator=(const UPopulationScratchBudget &)=delete;
private:
  UChunkManager &Manager;
  BufferBudgetContext Context;
  std::shared_ptr<UConcurrentAllocationCountingResource> Metadata;
  size_t Credit{0};
  void Reserve(size_t bytes) {
    if(!Context.budget) return;
    Context.budget->Reserve(*Context.job,bytes); Credit+=bytes;
  }
};
} // namespace cutum
