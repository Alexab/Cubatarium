#pragma once
#include "World/Chunks/KnownAirTerrainSlicePolicy.h"
#include "World/Chunks/TerrainColumnUtil.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/World.h"
#include "World/IO/ChunkStorageService.h"
#include "World/Math/GridMath.h"

namespace cutum
{
// Shared with opaque ray tracing: missing resident data is not automatically
// air. Completed columns can prove empty slices above all resident/disk data.
inline bool IsKnownAirTerrainSlice(const UWorld &world, glm::ivec3 chunk)
{
  KnownAirTerrainSliceEvidence e;
  e.sliceY = chunk.y;
  const int minY = world.GetProceduralSettings().MinHeight;
  const int maxY = world.GetProceduralSettings().MaxHeight;
  e.minSliceY = FloorDiv(minY, CHUNK_SIZE); e.maxSliceY = FloorDiv(maxY, CHUNK_SIZE);
  if (e.sliceY < e.minSliceY || e.sliceY > e.maxSliceY) return IsKnownAirTerrainSlice(e);
  const glm::ivec3 ground(chunk.x, 0, chunk.z);
  e.columnComplete = world.IsTerrainColumnCompleteFast(ground);
  if (!e.columnComplete) return false;
  const auto &folder = world.GetWorldFolderPath();
  e.hasWorldFolder = !folder.empty();
  if (!e.hasWorldFolder) return false;
  const auto &storage = world.GetChunkStorage();
  e.savePending = storage.IsColumnSavePending(ground);
  if (e.savePending) return false;
  e.highestResidentY = GetHighestNonAirChunkSlice(world.GetBlockWorld(), ground, maxY, minY);
  e.highestOnDiskY = storage.GetHighestChunkSliceOnDisk(folder, ground);
  return IsKnownAirTerrainSlice(e);
}
}
