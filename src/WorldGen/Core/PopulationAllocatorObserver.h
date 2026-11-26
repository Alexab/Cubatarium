#pragma once
#include <cstddef>
namespace cutum {
struct ChunkPopulateRequest;
class UBlockWorld;
class UPopulationScratchBudget;
class UChunkBuffer;
struct PopulationAllocatorView {
  const ChunkPopulateRequest &request;
  const UBlockWorld &world;
  const UPopulationScratchBudget &scratch;
  const UChunkBuffer &output;
};
class PopulationAllocatorObserver {
public:
  virtual ~PopulationAllocatorObserver()=default;
  virtual void Inspect(const PopulationAllocatorView &view)=0;
};
namespace detail {inline thread_local PopulationAllocatorObserver *ActivePopulationAllocatorObserver{};}
class PopulationAllocatorObserverScope {
public:
  explicit PopulationAllocatorObserverScope(PopulationAllocatorObserver *observer) noexcept
      :Previous(detail::ActivePopulationAllocatorObserver){detail::ActivePopulationAllocatorObserver=observer;}
  ~PopulationAllocatorObserverScope(){detail::ActivePopulationAllocatorObserver=Previous;}
  PopulationAllocatorObserverScope(const PopulationAllocatorObserverScope &)=delete;
  PopulationAllocatorObserverScope &operator=(const PopulationAllocatorObserverScope &)=delete;
private:PopulationAllocatorObserver *Previous{};
};
inline constexpr size_t PopulationAllocatorObserverInlineBytes(){return sizeof(detail::ActivePopulationAllocatorObserver);}
inline void InspectPopulationAllocatorView(const PopulationAllocatorView &view){
  if(auto *observer=detail::ActivePopulationAllocatorObserver)observer->Inspect(view);
}
}
