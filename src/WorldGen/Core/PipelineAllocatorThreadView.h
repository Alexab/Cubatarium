#pragma once
#include <cstddef>
#include <cstdint>
namespace cutum {
class AllocatorOwnerCensus;
class UPopulationScratchBudget;
struct PipelineAllocatorThreadView {
  bool initialized{},pipeline_present{};
  uint64_t builds{};
  size_t resident_fields{},tls_holder_bytes{};
};
// Calling thread only. The optional holder is observed without constructing
// a scratch world;active populations supply their actual stack scratch scope.
PipelineAllocatorThreadView InspectPipelineCallingThreadAllocators(AllocatorOwnerCensus &out,
    const UPopulationScratchBudget *active_scratch=nullptr);
}
