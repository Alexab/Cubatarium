#include "WorldGen/Core/WorldGenContentPinTls.h"
#include "WorldGen/Core/WorldGenContentPin.h"

namespace cutum
{

namespace
{
thread_local const WorldGenContentSnapshot *gPinnedContent = nullptr;
} // namespace

const WorldGenContentSnapshot *GetPinnedWorldGenContent()
{
  return gPinnedContent;
}

void SetPinnedWorldGenContent(const WorldGenContentSnapshot *snapshot)
{
  gPinnedContent = snapshot;
}

} // namespace cutum
