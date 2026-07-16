#pragma once

namespace cutum
{

struct WorldGenContentSnapshot;

/// TLS pin used by pack/feature/refs Get* under a Populate job.
/// Implemented in WorldGenContentPinTls.cpp (no Pack/Feature link deps).
const WorldGenContentSnapshot *GetPinnedWorldGenContent();
void SetPinnedWorldGenContent(const WorldGenContentSnapshot *snapshot);

} // namespace cutum
