#pragma once
#include "World/Core/BlockWorld.h"
namespace cutum {
class UWorld;class UCore;class UApplication;
// Named versions of the existing raw-owner captures. No ownership or state
// beyond the original pointer is added by these source-visible types.
struct WorldColumnMeshDirtyCallback {UWorld *owner;void operator()(int x,int z,int min_y,int max_y)const;};
struct WorldKnownAirFloorCallback {UWorld *owner;std::optional<int> operator()(glm::ivec3 column)const;};
struct WorldKnownAirVersionCallback {UWorld *owner;UBlockWorld::KnownAirEvidenceVersion operator()()const;};
struct WorldKnownAirSliceCallback {UWorld *owner;bool operator()(glm::ivec3 coord)const;};
struct WorldPopulationInvalidationCallback {UWorld *owner;void operator()(glm::ivec3 coord)const;};
struct CoreAfterWorldLoadedCallback {UCore *owner;void operator()()const;};
struct ApplicationBlockCatalogCallback {UApplication *owner;void operator()()const;};
struct ApplicationCreatureCatalogCallback {UApplication *owner;void operator()()const;};
} // namespace cutum
