#include "WorldGen/Core/WorldGenContentPinTls.h"
#include "WorldGen/Core/WorldGenContentPin.h"
#include "Core/Memory/RuntimeInlineSource.h"

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

size_t GetContentPinRuntimeInlineBytes() noexcept {return sizeof(gPinnedContent);}

} // namespace cutum
