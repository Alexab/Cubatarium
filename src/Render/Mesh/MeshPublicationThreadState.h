#pragma once
#include <string>
#include <cstdint>
namespace cutum {class UChunkMeshCache;}
namespace cutum::detail {
struct MeshPublicationThreadState {const UChunkMeshCache *LastCache{};std::string LastState;uint64_t LastFrame{};};
inline MeshPublicationThreadState &CallingMeshPublicationThreadState() noexcept {thread_local MeshPublicationThreadState state;return state;}
} // namespace cutum::detail
