#pragma once
#include "Core/Memory/RuntimeInlineSource.h"
#include "Core/Memory/BufferAllocationBudget.h"
#include "Core/Memory/ScopedSharedAllocation.h"
#include "Core/Memory/StbiAllocation.h"
#include "Core/Environment.h"
#include "Core/StallTimer.h"
#include "WorldGen/Core/CatalogJsonSource.h"
#include "WorldGen/Core/CatalogThreadReads.h"
#include "WorldGen/Core/PopulationAllocatorObserver.h"
#include <atomic>

namespace cutum
{
// No live TLS reads or initialization:all private providers return sizeof.
// W02 raw glog/noise cells and all dynamic contents stay in separate terms.
inline RuntimeInlineLayout GetRuntimeCoreInlineLayout() noexcept
{
  RuntimeInlineLayout out;const auto pipeline=GetPipelineRuntimeInlineLayout();
  out.Set(RuntimeInlineCell::LogAdapter,GetLogAdapterRuntimeInlineBytes());
  out.Set(RuntimeInlineCell::Population,pipeline.global);
  out.Set(RuntimeInlineCell::StbiProcess,sizeof(detail::StbiProcessLedger));
  out.Set(RuntimeInlineCell::JsonProcess,sizeof(detail::CatalogJsonSourceMutex)+sizeof(detail::CatalogJsonSources));
  out.Set(RuntimeInlineCell::StallFlag,sizeof(bool));
  out.Set(RuntimeInlineCell::Pipeline,pipeline.whole);
  out.Set(RuntimeInlineCell::PopulationClock,pipeline.clocks);
  out.Set(RuntimeInlineCell::Readers,4*sizeof(std::shared_ptr<const void>)+UBlockDefinitionStorage::ThreadReadInlineBytes()+sizeof(std::string));
  out.Set(RuntimeInlineCell::BlockProfiles,sizeof(BlockPhysicsProfile)+sizeof(FluidViewProfile)+sizeof(BlockAnimationSpec));
  out.Set(RuntimeInlineCell::MergeName,sizeof(std::string));
  out.Set(RuntimeInlineCell::ActiveBudget,sizeof(ActiveBufferBudget));
  out.Set(RuntimeInlineCell::ActiveShared,sizeof(ActiveScopedSharedResource));
  out.Set(RuntimeInlineCell::ActiveStbi,sizeof(detail::ActiveStbiLedger));
  out.Set(RuntimeInlineCell::StbiFailure,sizeof(detail::StbiThreadFailureGeneration));
  out.Set(RuntimeInlineCell::JsonShape,sizeof(detail::LastCatalogJsonSource));
  out.Set(RuntimeInlineCell::Pin,GetContentPinRuntimeInlineBytes());
  out.Set(RuntimeInlineCell::WorkerRole,GetWorkerRoleRuntimeInlineBytes());
  out.Set(RuntimeInlineCell::Features,GetFeatureRuntimeInlineBytes());
  out.Set(RuntimeInlineCell::StageClock,sizeof(std::chrono::steady_clock::time_point));
  out.Set(RuntimeInlineCell::EnvironmentVector,EnvironmentFlagCacheInlineBytes());
  out.Set(RuntimeInlineCell::ActiveBudgetSource,sizeof(ActiveBufferBudgetJobSource));
  out.Set(RuntimeInlineCell::PopulationObserver,PopulationAllocatorObserverInlineBytes());
  return out;
}
} // namespace cutum
