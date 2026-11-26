#pragma once
#include "World/Streaming/ColumnRecord.h"
#include <array>
namespace cutum {class UWorld;}
namespace cutum::detail {
struct FluidSurfaceMemo {
  bool valid{false};const UWorld *owner{};uint64_t epoch{};glm::ivec3 block{0};int radius{};bool value{};
};
struct WorldThreadState {
  std::array<FluidSurfaceMemo,4> FluidMemos{};size_t NextFluidMemo{};ColumnRecord FallbackRecord{};
};
inline WorldThreadState &CallingWorldThreadState() noexcept {thread_local WorldThreadState state;return state;}
} // namespace cutum::detail
